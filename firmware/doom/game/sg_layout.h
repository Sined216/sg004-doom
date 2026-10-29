/*
 * Where the Doom data lives on the SG-004A. Shared by the game firmware, the NOR loader and the host tools; the
 * Python side (tools/make_data.py) repeats these numbers and checks them against this file.
 *
 * DOOM.DAT (the converter's image) is cut in two:
 *   first SG_INT_BYTES  in the internal flash right behind the code (0x08030000-0x0806EFFF), part of the game .bin, so
 *                       it is written by the FNIRSI bootloader (inside the 0x08005800-0x0806F800 window)
 *   the rest            on the external W25Q80, from SG_NOR_MIN + 4 KB on, written by the NOR loader;
 *                       the 4 KB at SG_NOR_MIN is the header that ties both halves together.
 * The external flash below SG_NOR_MIN (sectors 0-55) is never written: sectors 0-30 are the stock History, 31-55 are
 * left free for it to grow.
 */
#pragma once

#define SG_INT_BASE      0x08030000u
#define SG_INT_CLUSTERS  63u
#define SG_INT_BYTES     (SG_INT_CLUSTERS * 4096u)      /* ends at 0x0806F000, below the window end 0x0806F800 */

#define SG_NOR_SIZE      0x100000u                      /* W25Q80, JEDEC EF 40 14 */
#define SG_NOR_MIN       0x38000u                       /* first byte the loader may erase or program */
#define SG_HDR_BYTES     4096u

/* header at NOR[SG_NOR_MIN]: magic(8) version(4) size of DOOM.DAT(4) bytes of it in the internal flash(4) image_uuid(16) */
#define SG_MAGIC         "SG04DOOM"
#define SG_VERSION       2u
#define SG_UUID_OFF_IN_DAT 20u                          /* image_uuid inside the DMC image header */

/* Reference CRCs of the owner's own backup of the external flash, given by the loader build
   (-DSG_HIST_CRC=0x...u -DSG_BACKUP_CHIP_CRC=0x...u, see doomload/build_write.sh). 0 means "no reference": the loader
   then only checks that the History area reads the same at the end as at the start, and refuses an area that is neither
   blank nor Doom data.
   SG_HIST_CRC        CRC-32 of NOR[0, SG_NOR_MIN): the loader refuses to write unless the History still reads like this
   SG_BACKUP_CHIP_CRC CRC-32 of the whole chip in the backup: after "erase Doom area" the chip must read like this again
                      (the area above SG_NOR_MIN was blank), which proves the way back to stock */
#ifndef SG_HIST_CRC
#define SG_HIST_CRC      0u
#endif
#ifndef SG_BACKUP_CHIP_CRC
#define SG_BACKUP_CHIP_CRC 0u
#endif
