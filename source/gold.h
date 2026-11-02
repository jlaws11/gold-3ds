/* Pokemon Gold (USA/Europe, rev 0) memory map.
 * Addresses come from pret/pokegold's symbol file (pokegold.sym). They are
 * only valid for the English Gold ROM checked in gold_rom_ok(). */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* WRAM */
#define wOverworldMapBlocks      0xC700   /* (w+6)*(h+6) block ids, 3-block border */
#define wMapTileset              0xD082
#define wEnvironment             0xD083   /* 1 town, 2 route, 3 indoor, 4 cave, 6 gate, 7 dungeon */
#define wMapHeight               0xD087
#define wMapWidth                0xD088
#define wTilesetBlocksBank       0xD0C5
#define wTilesetBlocksAddress    0xD0C6
#define wTilesetCollisionBank    0xD0C8
#define wTilesetCollisionAddress 0xD0C9
#define wTilesetPalettes         0xD0CF   /* pointer into bank 02 */
#define wBattleMode              0xD116
#define wMapStatus               0xD159   /* 2 = MAPSTATUS_HANDLE (overworld loop) */
#define wTimeOfDayPal            0xD568   /* 0 morn, 1 day, 2 nite, 3 dark */
#define wMapGroup                0xDA00
#define wMapNumber               0xDA01
#define wYCoord                  0xDA02
#define wXCoord                  0xDA03

/* HRAM: the game's clock (already includes the player's time offset) */
#define hHours                   0xFF96
#define hMinutes                 0xFF98

/* ROM tables (bank, address) */
#define ROM_COLLISION_PERMS_BANK 0x3E
#define ROM_COLLISION_PERMS_ADDR 0x74BE
#define ROM_BG_PALETTES_BANK     0x02
#define ROM_BG_PALETTES_ADDR     0x775E   /* 4 times of day x 8 palettes x 4 colours, RGB555 */

#define MAPSTATUS_HANDLE 2

/* A view of the running game: ROM bytes plus a way to read the CPU address space. */
typedef struct {
	const uint8_t *rom;
	size_t rom_size;
	uint8_t (*peek)(void *ctx, uint16_t addr);
	void *ctx;
} GoldView;

static inline uint8_t gv8(const GoldView *g, uint16_t a) { return g->peek(g->ctx, a); }
static inline uint16_t gv16(const GoldView *g, uint16_t a) { return gv8(g, a) | (gv8(g, a + 1) << 8); }
static inline uint32_t rom_offset(uint8_t bank, uint16_t addr) {
	return addr < 0x4000 ? addr : (uint32_t)bank * 0x4000 + (addr - 0x4000);
}

/* Returns NULL when the ROM is the supported English Gold, else a reason. */
const char *gold_rom_check(const uint8_t *rom, size_t size);
