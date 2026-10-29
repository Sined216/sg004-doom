"""Instruction-level test of the SG-004A Doom firmware on the PC (Unicorn, Cortex-M4).

Loads the .bin at 0x08005800, runs it from the reset vector and models just enough hardware to see what the
firmware does: clock and reset registers, GPIO inputs (keys), SPI2 with the NOR image, the ST7789 on FSMC (the
screen is rebuilt into a 320x240 picture), SysTick as an injected exception. It cannot say anything about real
timings, only about what the code does and draws.

usage: python3 -I emu_run.py BIN ELF NOR.img OUTDIR [--seconds 10] [--keys "3.0:OK:0.2,5:UP:1"] [--shots 1.0]
  --seconds N   emulated seconds to run (1 ms = INSTR_PER_MS instructions)
  --keys        scripted presses: start_s:KEY:duration_s, KEY in IN OUT FN UP DOWN LEFT RIGHT OK POWER
  --shots S     write a PNG of the screen every S emulated seconds
"""
import argparse
import os
import struct
import subprocess
import sys
import zlib

from unicorn import (Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE,
                     UC_HOOK_CODE, UC_PROT_ALL, UcError, UC_ERR_OK, UC_HOOK_MEM_UNMAPPED)
from unicorn.arm_const import (UC_ARM_REG_PC, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
                               UC_ARM_REG_R3, UC_ARM_REG_R12, UC_ARM_REG_XPSR, UC_ARM_REG_MSP, UC_CPU_ARM_CORTEX_M4)

FLASH_BASE = 0x08005800
INSTR_PER_MS = 60000          # emulated instructions that make one millisecond of SysTick

ap = argparse.ArgumentParser()
ap.add_argument("bin")
ap.add_argument("elf")
ap.add_argument("nor")
ap.add_argument("outdir")
ap.add_argument("--seconds", type=float, default=10.0)
ap.add_argument("--keys", default="")
ap.add_argument("--shots", type=float, default=1.0)
ap.add_argument("--poke", default="", help="comma separated t_seconds:symbol:value, writes a 32-bit word at that time")
ap.add_argument("--trace", default="", help="comma separated function names: print arguments and result of every call")
a = ap.parse_args()
os.makedirs(a.outdir, exist_ok=True)

# ---- symbols from the ELF
nm = subprocess.run(["arm-none-eabi-nm", a.elf], capture_output=True, text=True,
                    env=dict(os.environ, PATH=r"C:\msys64\ucrt64\bin;" + os.environ["PATH"])).stdout
sym = {}
for ln in nm.splitlines():
    p = ln.split()
    if len(p) == 3:
        sym[p[2]] = int(p[0], 16)

image = open(a.bin, "rb").read()
nor = open(a.nor, "rb").read()

uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
uc.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M4)
uc.mem_map(0x08000000, 0x100000, UC_PROT_ALL)
uc.mem_map(0x20000000, 0x40000, UC_PROT_ALL)
uc.mem_map(0x40000000, 0x100000, UC_PROT_ALL)
uc.mem_map(0x60000000, 0x40000, UC_PROT_ALL)
uc.mem_map(0xA0000000, 0x1000, UC_PROT_ALL)
uc.mem_map(0xE0000000, 0x100000, UC_PROT_ALL)          # system control space (SysTick, SCB, NVIC) as plain memory

uc.mem_write(0x08000000, bytes([255]) * 0x100000)               # erased flash reads 0xFF
uc.mem_write(FLASH_BASE, image)
TRAMP = 0x20030000                                       # exception-return trampoline in unused RAM

# ---- registers of the peripherals the firmware touches
RCC = 0x40021000
rcc = {0x00: 0x83, 0x04: 0}
keys_down = set()
KEYPIN = {                       # name: (GPIO IDR address, bit)
    "IN": (0x40011008, 14), "OUT": (0x40011008, 15), "FN": (0x40011008, 13), "UP": (0x40011008, 12),
    "DOWN": (0x40010C08, 4), "LEFT": (0x40011408, 2), "RIGHT": (0x40010C08, 6), "OK": (0x40010C08, 3),
    "POWER": (0x40010C08, 8),
}
IDR = {0x40010808: 0xFFFF, 0x40010C08: 0xFFFF, 0x40011008: 0xFFFF, 0x40011408: 0xFFFF, 0x40011808: 0xFFFF}

# NOR over SPI2
SPI_DR, SPI_SR = 0x4000380C, 0x40003808
cs_low = False
nor_state = {"cmd": None, "n": 0, "addr": 0}
spi_rx = 0xFF
nor_bytes = 0
ID = bytes([0xEF, 0x40, 0x17])         # W25Q64-like


def gpiob_bits(addr, value):
    global cs_low, nor_state
    if addr == 0x40010C10:                    # BSRR: low half sets, high half resets
        if value & (1 << 12):
            cs_low = False
        if value & (1 << 28):
            cs_low = True
            nor_state = {"cmd": None, "n": 0, "addr": 0}
    elif addr == 0x40010C14:                  # BRR
        if value & (1 << 12):
            cs_low = True
            nor_state = {"cmd": None, "n": 0, "addr": 0}


def spi_xfer(b):
    global nor_bytes
    s = nor_state
    if not cs_low:
        return 0xFF
    if s["cmd"] is None:
        s["cmd"] = b
        s["n"] = 0
        return 0xFF
    n = s["n"]
    s["n"] += 1
    if s["cmd"] == 0x9F:
        return ID[n] if n < 3 else 0xFF
    if s["cmd"] == 0x03:
        if n < 3:
            s["addr"] = (s["addr"] << 8) | b
            return 0xFF
        a_ = s["addr"] + (n - 3)
        nor_bytes += 1
        return nor[a_] if a_ < len(nor) else 0xFF
    return 0xFF


# ST7789 model on FSMC
LCD_CMD, LCD_DATA = 0x60000000, 0x60020000
fb = bytearray(320 * 240 * 2)
lcd = {"cmd": 0, "params": [], "x0": 0, "x1": 319, "y0": 0, "y1": 239, "x": 0, "y": 0, "px": 0, "dirty": False, "cmds": []}


def lcd_write(addr, value):
    L = lcd
    if addr == LCD_CMD:
        L["cmd"] = value & 0xFF
        L["params"] = []
        L["cmds"].append(value & 0xFF)
        if L["cmd"] == 0x2C:
            L["x"], L["y"] = L["x0"], L["y0"]
        return
    if L["cmd"] == 0x2C:
        if 0 <= L["x"] < 320 and 0 <= L["y"] < 240:
            o = (L["y"] * 320 + L["x"]) * 2
            fb[o] = value & 0xFF
            fb[o + 1] = (value >> 8) & 0xFF
        L["px"] += 1
        L["dirty"] = True
        L["x"] += 1
        if L["x"] > L["x1"]:
            L["x"] = L["x0"]
            L["y"] += 1
            if L["y"] > L["y1"]:
                L["y"] = L["y0"]
        return
    L["params"].append(value & 0xFFFF)
    p = L["params"]
    if L["cmd"] == 0x2A and len(p) == 4:
        L["x0"], L["x1"] = (p[0] << 8) | (p[1] & 0xFF), (p[2] << 8) | (p[3] & 0xFF)
    elif L["cmd"] == 0x2B and len(p) == 4:
        L["y0"], L["y1"] = (p[0] << 8) | (p[1] & 0xFF), (p[2] << 8) | (p[3] & 0xFF)


def write_png(path):
    rows = bytearray()
    for y in range(240):
        rows.append(0)
        for x in range(320):
            o = (y * 320 + x) * 2
            v = fb[o] | (fb[o + 1] << 8)
            r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
            rows += bytes(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)))

    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 320, 240, 8, 2, 0, 0, 0)) \
        + chunk(b"IDAT", zlib.compress(bytes(rows), 6)) + chunk(b"IEND", b"")
    open(path, "wb").write(png)


# ---- hooks
def on_write(uc_, access, address, size, value, user):
    if address in (LCD_CMD, LCD_DATA):
        lcd_write(address, value)
        return
    if RCC <= address < RCC + 0x40:
        rcc[address - RCC] = value & 0xFFFFFFFF
        return
    if address in (0x40010C10, 0x40010C14):
        gpiob_bits(address, value & 0xFFFFFFFF)
        return
    if address == SPI_DR:
        global spi_rx
        spi_rx = spi_xfer(value & 0xFF)


def on_read(uc_, access, address, size, value, user):
    if RCC <= address < RCC + 0x40:
        off = address - RCC
        if off == 0x00:
            v = rcc[0] | 0x2                           # HSIRDY
            v = (v | (1 << 17)) if v & (1 << 16) else (v & ~(1 << 17))
            v = (v | (1 << 25)) if v & (1 << 24) else (v & ~(1 << 25))
            uc_.mem_write(address, struct.pack("<I", v & 0xFFFFFFFF))
        elif off == 0x04:
            v = rcc[4]
            v = (v & ~0xC) | ((v & 3) << 2)            # SWS follows SW
            uc_.mem_write(address, struct.pack("<I", v & 0xFFFFFFFF))
        return
    if address in IDR:
        v = 0xFFFF
        for k in keys_down:
            ad, bit = KEYPIN[k]
            if ad == address:
                v &= ~(1 << bit)
        uc_.mem_write(address, struct.pack("<I", v))
        return
    if address == SPI_DR:
        uc_.mem_write(address, struct.pack("<H", spi_rx))
    elif address == SPI_SR:
        uc_.mem_write(address, struct.pack("<H", 3))


def on_unmapped(uc_, access, address, size, value, user):
    print("UNMAPPED access type=%d address=0x%08X size=%d pc=0x%08X" % (access, address, size, uc_.reg_read(UC_ARM_REG_PC)))
    return False


uc.hook_add(UC_HOOK_MEM_UNMAPPED, on_unmapped)
uc.hook_add(UC_HOOK_MEM_WRITE, on_write, begin=0x40000000, end=0x400FFFFF)
uc.hook_add(UC_HOOK_MEM_WRITE, on_write, begin=0x60000000, end=0x6003FFFF)
uc.hook_add(UC_HOOK_MEM_READ, on_read, begin=0x40000000, end=0x400FFFFF)

# ---- schedule of key presses
sched = []
for item in filter(None, a.keys.split(",")):
    t, k, d = item.split(":")
    sched.append((float(t), k.upper(), float(d)))

pokes = []
for item in filter(None, a.poke.split(",")):
    t, name, val = item.split(":")
    pokes.append([float(t), name, int(val, 0), False])

# ---- SysTick as an injected exception
hnd = struct.unpack("<I", image[15 * 4:16 * 4])[0] & ~1
in_irq = []


def enter_systick():
    sp = uc.reg_read(UC_ARM_REG_SP)
    regs = [uc.reg_read(r) for r in (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_R12,
                                     UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_XPSR)]
    sp -= 32
    uc.mem_write(sp, struct.pack("<8I", *regs))
    uc.reg_write(UC_ARM_REG_SP, sp)
    uc.reg_write(UC_ARM_REG_LR, TRAMP | 1)
    in_irq.append(True)
    return hnd


def leave_systick():
    sp = uc.reg_read(UC_ARM_REG_SP)
    r = struct.unpack("<8I", uc.mem_read(sp, 32))
    uc.reg_write(UC_ARM_REG_SP, sp + 32)
    for reg, val in zip((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_R12, UC_ARM_REG_LR), r[:6]):
        uc.reg_write(reg, val)
    uc.reg_write(UC_ARM_REG_XPSR, r[7])      # flags and IT state of the interrupted code
    in_irq.pop()
    return r[6]


# ---- run
init_sp, reset = struct.unpack("<II", image[:8])
uc.reg_write(UC_ARM_REG_MSP, init_sp)
uc.reg_write(UC_ARM_REG_SP, init_sp)
pc = reset & ~1

fault_addrs = {sym.get("fault_screen"): "fault_screen", sym.get("plat_panic"): "plat_panic"}
stop_reason = None
fault_info = {}


def code_hook(uc_, address, size, user):
    global stop_reason
    if address == TRAMP:
        uc_.emu_stop()
    elif address in fault_addrs:
        stop_reason = fault_addrs[address]
        fault_info.update(r0=uc_.reg_read(UC_ARM_REG_R0), r1=uc_.reg_read(UC_ARM_REG_R1), lr=uc_.reg_read(UC_ARM_REG_LR),
                          sp=uc_.reg_read(UC_ARM_REG_SP), pc=address)
        uc_.emu_stop()


trace_names = [n for n in a.trace.split(",") if n]
call_n = {n: 0 for n in trace_names}


def make_trace(name):
    def entry(uc_, address, size, user):
        call_n[name] += 1
        lr = uc_.reg_read(UC_ARM_REG_LR)
        args = [uc_.reg_read(r) for r in (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3)]
        k = call_n[name]
        print("  [%d ms] call %s#%d(%s) from 0x%08X" % (ms, name, k, ", ".join("0x%X" % v for v in args), lr & ~1))
        handle = []

        def ret(uc2, address2, size2, user2):
            print("  [%d ms]   %s#%d -> %d (0x%X)" % (ms, name, k, uc2.reg_read(UC_ARM_REG_R0) - (1 << 32) if uc2.reg_read(UC_ARM_REG_R0) >= 1 << 31 else uc2.reg_read(UC_ARM_REG_R0), uc2.reg_read(UC_ARM_REG_R0)))
            if handle:
                uc2.hook_del(handle.pop())
        handle.append(uc_.hook_add(UC_HOOK_CODE, ret, begin=lr & ~1, end=lr & ~1))
    return entry


for n in trace_names:
    if n in sym:
        ad = sym[n] & ~1
        uc.hook_add(UC_HOOK_CODE, make_trace(n), begin=ad, end=ad)
    else:
        print("no symbol", n)

uc.hook_add(UC_HOOK_CODE, code_hook, begin=TRAMP, end=TRAMP)
for ad in [x for x in fault_addrs if x]:
    uc.hook_add(UC_HOOK_CODE, code_hook, begin=ad, end=ad)

total_ms = int(a.seconds * 1000)
shots_every = int(a.shots * 1000)
ms = 0
executed = 0
last_shot = 0
shot_n = 0
cur_keys = {}
print("start: pc=0x%08X sp=0x%08X" % (pc, init_sp))
try:
    while ms < total_ms and stop_reason is None:
        # scripted keys
        for (t, k, d) in sched:
            if t * 1000 <= ms < (t + d) * 1000:
                keys_down.add(k)
            elif k in keys_down and ms >= (t + d) * 1000:
                keys_down.discard(k)
        for pk in pokes:
            if not pk[3] and ms >= pk[0] * 1000:
                pk[3] = True
                uc.mem_write(sym[pk[1]], struct.pack("<I", pk[2]))
                print("  [%d ms] poke %s = %d" % (ms, pk[1], pk[2]))
        # one millisecond of instructions (SysTick runs between chunks)
        uc.emu_start(pc | 1, 0xFFFFFFFE, count=INSTR_PER_MS)
        pc = uc.reg_read(UC_ARM_REG_PC)
        if in_irq and pc == TRAMP:
            pc = leave_systick()
        elif not in_irq:
            hpc = enter_systick()
            # run the handler to its return
            uc.emu_start(hpc | 1, TRAMP, count=20000)
            pc2 = uc.reg_read(UC_ARM_REG_PC)
            if pc2 == TRAMP or in_irq:
                pc = leave_systick()
        ms += 1
        if shots_every and ms - last_shot >= shots_every:
            last_shot = ms
            shot_n += 1
            write_png(os.path.join(a.outdir, "shot_%03d.png" % shot_n))
            print("t=%5.2fs ms_ticks=%s LCD px=%d nor=%d B  keys=%s" % (
                ms / 1000.0, struct.unpack("<I", uc.mem_read(sym["ms_ticks"], 4))[0] if "ms_ticks" in sym else "?",
                lcd["px"], nor_bytes, sorted(keys_down)))
except UcError as e:
    pc = uc.reg_read(UC_ARM_REG_PC)
    print("EMULATION ERROR: %s at pc=0x%08X lr=0x%08X sp=0x%08X" % (e, pc, uc.reg_read(UC_ARM_REG_LR), uc.reg_read(UC_ARM_REG_SP)))
    stop_reason = "error"

write_png(os.path.join(a.outdir, "final.png"))
print("stopped: %s after %d emulated ms, pc=0x%08X" % (stop_reason or "time limit", ms, uc.reg_read(UC_ARM_REG_PC)))
if stop_reason == "plat_panic":
    msg = bytes(uc.mem_read(fault_info["r0"], 64)).split(bytes(1))[0].decode("latin1")
    print("PANIC: %r  called from lr=0x%08X" % (msg, fault_info["lr"]))
elif stop_reason == "fault_screen":
    fr = fault_info["r0"]
    if fr:
        pcf, lrf = struct.unpack("<II", uc.mem_read(fr + 24, 8))[0:2], None
        regs = struct.unpack("<8I", uc.mem_read(fr, 32))
        print("FAULT: stacked pc=0x%08X lr=0x%08X exc_return=0x%08X" % (regs[6], regs[5], fault_info["r1"]))
    else:
        print("fault_screen entered with no frame (r1=0x%X) lr=0x%08X" % (fault_info["r1"], fault_info["lr"]))
