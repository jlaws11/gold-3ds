/* Thin wrapper around the Peanut-GB emulator core, so the core can be swapped
 * (a Game Boy Color core is the planned upgrade) without touching the rest. */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

/* buttons for core_frame() */
#define BTN_A      0x01
#define BTN_B      0x02
#define BTN_SELECT 0x04
#define BTN_START  0x08
#define BTN_RIGHT  0x10
#define BTN_LEFT   0x20
#define BTN_UP     0x40
#define BTN_DOWN   0x80

#define CORE_SRAM_SIZE 0x8000

/* rom must stay valid while the core runs. sram may be NULL (new save). */
const char *core_init(const uint8_t *rom, size_t rom_size, const uint8_t *sram, size_t sram_size);
void core_set_clock(const struct tm *t);
void core_frame(uint8_t buttons);          /* runs one Game Boy frame */
uint8_t core_peek(void *ctx, uint16_t addr);
const uint8_t *core_lcd(void);             /* 160x144 shades 0..3 (bits 4-5: layer) */
uint8_t *core_sram(void);
uint32_t core_sram_hash(void);
