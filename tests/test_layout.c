#include "minunit.h"
#include "km_layout.h"

/* A stand-in for the firmware's hid_asciimap: 'a'..'c' map, everything
 * else is unmappable. Index == ASCII code. */
static uint16_t stub_map[128];

static void build_stub(void) {
    for(size_t i = 0; i < 128; i++) stub_map[i] = KM_KEY_NONE;
    stub_map['a'] = 0x04;
    stub_map['b'] = 0x05;
    stub_map['c'] = 0x06;
    stub_map['A'] = 0x04 | (1 << 9); /* shifted */
}

static void test_default_seeds_from_table(void) {
    build_stub();
    KmLayout l;
    km_layout_set_default(&l, stub_map, 128);
    CHECK(km_layout_lookup(&l, 'a') == 0x04, "a maps");
    CHECK(km_layout_lookup(&l, 'A') == (0x04 | (1 << 9)), "A maps with shift");
    CHECK(km_layout_lookup(&l, 'z') == KM_KEY_NONE, "z unmapped");
}

static void test_short_table_pads_with_none(void) {
    build_stub();
    KmLayout l;
    km_layout_set_default(&l, stub_map, 8); /* only first 8 entries provided */
    CHECK(km_layout_lookup(&l, 'a') == KM_KEY_NONE, "beyond provided range is none");
}

static void test_high_bytes_never_map(void) {
    build_stub();
    KmLayout l;
    km_layout_set_default(&l, stub_map, 128);
    CHECK(km_layout_lookup(&l, 0xC3) == KM_KEY_NONE, "0xC3 unmappable");
    CHECK(km_layout_lookup(&l, 0xFF) == KM_KEY_NONE, "0xFF unmappable");
}

static void test_first_unmappable_finds_index(void) {
    build_stub();
    KmLayout l;
    km_layout_set_default(&l, stub_map, 128);

    const uint8_t good[] = {'a', 'b', 'c'};
    CHECK(km_layout_first_unmappable(&l, good, 3) == -1, "all good");

    const uint8_t bad[] = {'a', 'b', 'c', 'z'};
    CHECK(km_layout_first_unmappable(&l, bad, 4) == 3, "index 3");

    const uint8_t utf8[] = {'a', 0xC3, 0xA9}; /* 'a' then UTF-8 e-acute */
    CHECK(km_layout_first_unmappable(&l, utf8, 3) == 1, "utf8 index 1");
}

static void test_load_bytes_requires_exact_size(void) {
    KmLayout l;
    uint8_t data[KM_LAYOUT_FILE_BYTES];
    for(size_t i = 0; i < sizeof(data); i++) data[i] = 0;

    CHECK(km_layout_load_bytes(&l, data, KM_LAYOUT_FILE_BYTES) == true, "exact size ok");
    CHECK(km_layout_load_bytes(&l, data, 255) == false, "255 rejected");
    CHECK(km_layout_load_bytes(&l, data, 257) == false, "257 rejected");
    CHECK(km_layout_load_bytes(&l, data, 0) == false, "0 rejected");
}

static void test_load_bytes_reads_little_endian(void) {
    KmLayout l;
    uint8_t data[KM_LAYOUT_FILE_BYTES];
    for(size_t i = 0; i < sizeof(data); i++) data[i] = 0;
    /* index 'a' == 97, byte offset 194: little-endian 0x0204 */
    data[97 * 2] = 0x04;
    data[97 * 2 + 1] = 0x02;

    CHECK(km_layout_load_bytes(&l, data, KM_LAYOUT_FILE_BYTES) == true, "loaded");
    CHECK(km_layout_lookup(&l, 'a') == 0x0204, "little-endian decode");
}

int main(void) {
    RUN(test_default_seeds_from_table);
    RUN(test_short_table_pads_with_none);
    RUN(test_high_bytes_never_map);
    RUN(test_first_unmappable_finds_index);
    RUN(test_load_bytes_requires_exact_size);
    RUN(test_load_bytes_reads_little_endian);
    REPORT();
}
