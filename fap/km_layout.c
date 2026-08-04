#include "km_layout.h"

void km_layout_set_default(KmLayout* layout, const uint16_t* asciimap, size_t asciimap_entries) {
    for(size_t i = 0; i < KM_LAYOUT_SIZE; i++) {
        layout->map[i] = (i < asciimap_entries) ? asciimap[i] : KM_KEY_NONE;
    }
}

bool km_layout_load_bytes(KmLayout* layout, const uint8_t* data, size_t len) {
    if(len != KM_LAYOUT_FILE_BYTES) return false;
    for(size_t i = 0; i < KM_LAYOUT_SIZE; i++) {
        layout->map[i] = (uint16_t)data[i * 2] | ((uint16_t)data[i * 2 + 1] << 8);
    }
    return true;
}

uint16_t km_layout_lookup(const KmLayout* layout, uint8_t c) {
    if(c >= KM_LAYOUT_SIZE) return KM_KEY_NONE;
    return layout->map[c];
}

int km_layout_first_unmappable(const KmLayout* layout, const uint8_t* text, size_t len) {
    for(size_t i = 0; i < len; i++) {
        if(km_layout_lookup(layout, text[i]) == KM_KEY_NONE) return (int)i;
    }
    return -1;
}
