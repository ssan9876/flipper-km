#pragma once
#include "km_layout.h"
#include <stdbool.h>
#include <stddef.h>

#define KM_LAYOUT_DIR "/ext/badusb/assets/layouts"
#define KM_SETTINGS_DIR "/ext/apps_data/km_bridge"
#define KM_SETTINGS_PATH KM_SETTINGS_DIR "/layout.txt"

bool km_layout_file_load(KmLayout* layout, const char* path);
void km_layout_settings_save(const char* path);
bool km_layout_settings_load(char* path_out, size_t cap);
