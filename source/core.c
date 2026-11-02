#include "core.h"
#include <string.h>

#define ENABLE_SOUND 0
#define ENABLE_LCD 1
#define PEANUT_GB_IS_LITTLE_ENDIAN 1
#include "../third_party/peanut_gb.h"

static struct gb_s gb;
static const uint8_t *g_rom;
static size_t g_rom_size;
static uint8_t g_sram[CORE_SRAM_SIZE];
static uint8_t g_lcd[144][160];

static uint8_t rom_read(struct gb_s *g, const uint_fast32_t a) { (void)g; return a < g_rom_size ? g_rom[a] : 0xFF; }
static uint8_t ram_read(struct gb_s *g, const uint_fast32_t a) { (void)g; return g_sram[a % CORE_SRAM_SIZE]; }
static void ram_write(struct gb_s *g, const uint_fast32_t a, const uint8_t v) { (void)g; g_sram[a % CORE_SRAM_SIZE] = v; }
static void on_error(struct gb_s *g, const enum gb_error_e e, const uint16_t a) { (void)g; (void)e; (void)a; }
static void on_line(struct gb_s *g, const uint8_t px[160], const uint_fast8_t y) {
	(void)g;
	if (y < 144) memcpy(g_lcd[y], px, 160);
}

const char *core_init(const uint8_t *rom, size_t rom_size, const uint8_t *sram, size_t sram_size) {
	g_rom = rom; g_rom_size = rom_size;
	memset(g_sram, 0xFF, sizeof g_sram);
	if (sram) memcpy(g_sram, sram, sram_size < sizeof g_sram ? sram_size : sizeof g_sram);
	switch (gb_init(&gb, rom_read, ram_read, ram_write, on_error, NULL)) {
	case GB_INIT_NO_ERROR: break;
	case GB_INIT_CARTRIDGE_UNSUPPORTED: return "The emulator does not support this cartridge type.";
	case GB_INIT_INVALID_CHECKSUM: return "The ROM header checksum is wrong.";
	default: return "The emulator could not start.";
	}
	gb_init_lcd(&gb, on_line);
	return NULL;
}

void core_set_clock(const struct tm *t) { gb_set_rtc(&gb, t); }

void core_frame(uint8_t buttons) {
	gb.direct.joypad = (uint8_t)~buttons;   /* Peanut-GB: 0 = pressed */
	gb_run_frame(&gb);
}

uint8_t core_peek(void *ctx, uint16_t addr) { (void)ctx; return __gb_read(&gb, addr); }
const uint8_t *core_lcd(void) { return &g_lcd[0][0]; }
uint8_t *core_sram(void) { return g_sram; }

uint32_t core_sram_hash(void) {
	uint32_t h = 2166136261u;
	for (size_t i = 0; i < sizeof g_sram; i++) h = (h ^ g_sram[i]) * 16777619u;
	return h;
}
