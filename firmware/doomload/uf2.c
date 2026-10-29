#include <string.h>
#include "../doom/game/sg_layout.h"
#include "nor.h"
#include "nor_write.h"
#include "uf2.h"

#define UF2_MAGIC0     0x0A324655u
#define UF2_MAGIC1     0x9E5D5157u
#define UF2_MAGIC_END  0x0AB16F30u
#define UF2_FLAG_FAMILY 0x00002000u
#define UF2_FAMILY     0x34304753u          /* "SG04" */

#define PAGES (SG_NOR_SIZE / 256u)          /* 4096 */
#define SECTORS (SG_NOR_SIZE / 4096u)       /* 256 */

struct uf2_stats uf2;
static uint8_t page_done[PAGES / 8];
static uint8_t sector_erased[SECTORS / 8];

static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

void uf2_reset(void)
{
    memset(&uf2, 0, sizeof uf2);
    memset(page_done, 0, sizeof page_done);
    memset(sector_erased, 0, sizeof sector_erased);
}

void uf2_block(uint32_t lba, const uint8_t *b)
{
    (void)lba;
    if (le32(b) != UF2_MAGIC0 || le32(b + 4) != UF2_MAGIC1 || le32(b + 508) != UF2_MAGIC_END) {
        uf2.ignored++;
        return;
    }
    uint32_t flags = le32(b + 8), addr = le32(b + 12), size = le32(b + 16), nblk = le32(b + 24), family = le32(b + 28);
    if (!(flags & UF2_FLAG_FAMILY) || family != UF2_FAMILY || size != 256u || nblk == 0 || nblk > PAGES
        || (addr & 255u) || addr < SG_NOR_MIN || addr >= SG_NOR_SIZE) {
        uf2.rejected++;
        return;
    }
    if (uf2.total == 0) uf2.total = nblk;
    else if (uf2.total != nblk) { uf2.rejected++; return; }
    uf2.last_addr = addr;

    const uint8_t *data = b + 32;
    uint32_t page = addr >> 8, sector = addr >> 12;
    if (page_done[page >> 3] & (1u << (page & 7))) {                 /* seen before in this session */
        uint8_t cur[256];
        nor_read(addr, cur, sizeof cur);
        if (!memcmp(cur, data, 256)) uf2.dup++;
        else uf2.mismatch++;
        return;
    }
    int r;
    if (!(sector_erased[sector >> 3] & (1u << (sector & 7)))) {
        if ((r = nor_wr_erase4k(sector << 12))) { uf2.errors++; uf2.last_err = r; return; }
        sector_erased[sector >> 3] |= (uint8_t)(1u << (sector & 7));
        uf2.erased++;
    }
    if ((r = nor_wr_page(addr, data))) { uf2.errors++; uf2.last_err = r; return; }
    page_done[page >> 3] |= (uint8_t)(1u << (page & 7));
    uf2.ok++;
}

int uf2_complete(void)
{
    return uf2.total && uf2.ok == uf2.total && !uf2.errors && !uf2.mismatch && !uf2.rejected;
}
