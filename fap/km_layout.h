#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KM_LAYOUT_SIZE 128
#define KM_KEY_NONE 0
#define KM_LAYOUT_FILE_BYTES (KM_LAYOUT_SIZE * 2u) /* uint16_t[128] == 256 bytes */

typedef struct {
    uint16_t map[KM_LAYOUT_SIZE];
} KmLayout;

/** Seed the layout from the firmware's hid_asciimap (or any table). Entries
 *  beyond asciimap_entries, and beyond KM_LAYOUT_SIZE, become KM_KEY_NONE. */
void km_layout_set_default(KmLayout* layout, const uint16_t* asciimap, size_t asciimap_entries);

/** Load a Bad USB .kl file body. Requires exactly KM_LAYOUT_FILE_BYTES bytes. */
bool km_layout_load_bytes(KmLayout* layout, const uint8_t* data, size_t len);

/** Keycode for a byte, or KM_KEY_NONE if unmappable (includes all bytes >= 128). */
uint16_t km_layout_lookup(const KmLayout* layout, uint8_t c);

/** -1 if every byte maps, else the zero-based index of the first that does not. */
int km_layout_first_unmappable(const KmLayout* layout, const uint8_t* text, size_t len);
