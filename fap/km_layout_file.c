#include "km_layout_file.h"

#include <furi.h>
#include <storage/storage.h>
#include <string.h>

bool km_layout_file_load(KmLayout* layout, const char* path) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    bool ok = false;

    if(storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        uint8_t data[KM_LAYOUT_FILE_BYTES];
        size_t read = storage_file_read(file, data, sizeof(data));
        if(read == KM_LAYOUT_FILE_BYTES) {
            ok = km_layout_load_bytes(layout, data, read);
        }
        storage_file_close(file);
    }

    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    return ok;
}

/* This file holds only a layout path -- no secrets. */
void km_layout_settings_save(const char* path) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_common_mkdir(storage, KM_SETTINGS_DIR);

    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, KM_SETTINGS_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        storage_file_write(file, path, strlen(path));
        storage_file_close(file);
    }
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
}

bool km_layout_settings_load(char* path_out, size_t cap) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    bool ok = false;

    if(storage_file_open(file, KM_SETTINGS_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        size_t read = storage_file_read(file, path_out, cap - 1);
        path_out[read] = '\0';
        ok = read > 0;
        storage_file_close(file);
    }

    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    return ok;
}
