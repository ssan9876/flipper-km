#include "minunit.h"
#include "km_base64.h"

static void test_decodes_empty(void) {
    uint8_t out[8];
    size_t out_len = 99;
    CHECK(km_base64_decode("", 0, out, sizeof(out), &out_len) == KmB64Ok, "empty ok");
    CHECK(out_len == 0, "empty length");
}

static void test_decodes_one_two_three_bytes(void) {
    uint8_t out[8];
    size_t out_len = 0;

    CHECK(km_base64_decode("QQ==", 4, out, sizeof(out), &out_len) == KmB64Ok, "QQ== ok");
    CHECK(out_len == 1 && out[0] == 'A', "QQ== -> A");

    CHECK(km_base64_decode("QUI=", 4, out, sizeof(out), &out_len) == KmB64Ok, "QUI= ok");
    CHECK(out_len == 2 && memcmp(out, "AB", 2) == 0, "QUI= -> AB");

    CHECK(km_base64_decode("QUJD", 4, out, sizeof(out), &out_len) == KmB64Ok, "QUJD ok");
    CHECK(out_len == 3 && memcmp(out, "ABC", 3) == 0, "QUJD -> ABC");
}

static void test_preserves_embedded_null(void) {
    /* base64 of the three bytes 'a', 0x00, 'b' */
    uint8_t out[8];
    size_t out_len = 0;
    CHECK(km_base64_decode("YQBi", 4, out, sizeof(out), &out_len) == KmB64Ok, "YQBi ok");
    CHECK(out_len == 3, "embedded null length");
    CHECK(out[0] == 'a' && out[1] == 0 && out[2] == 'b', "embedded null bytes");
}

static void test_rejects_bad_length(void) {
    uint8_t out[8];
    size_t out_len = 0;
    CHECK(km_base64_decode("QQ=", 3, out, sizeof(out), &out_len) == KmB64BadLength, "len 3");
    CHECK(km_base64_decode("QQQQQ", 5, out, sizeof(out), &out_len) == KmB64BadLength, "len 5");
}

static void test_rejects_bad_char(void) {
    uint8_t out[8];
    size_t out_len = 0;
    CHECK(km_base64_decode("QQ$=", 4, out, sizeof(out), &out_len) == KmB64BadChar, "dollar");
    CHECK(km_base64_decode("QQ =", 4, out, sizeof(out), &out_len) == KmB64BadChar, "space");
}

static void test_rejects_overlong_output(void) {
    uint8_t out[2];
    size_t out_len = 0;
    CHECK(km_base64_decode("QUJD", 4, out, sizeof(out), &out_len) == KmB64TooLong, "cap 2");
}

int main(void) {
    RUN(test_decodes_empty);
    RUN(test_decodes_one_two_three_bytes);
    RUN(test_preserves_embedded_null);
    RUN(test_rejects_bad_length);
    RUN(test_rejects_bad_char);
    RUN(test_rejects_overlong_output);
    REPORT();
}
