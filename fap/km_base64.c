#include "km_base64.h"

static int b64_value(char c) {
    if(c >= 'A' && c <= 'Z') return c - 'A';
    if(c >= 'a' && c <= 'z') return c - 'a' + 26;
    if(c >= '0' && c <= '9') return c - '0' + 52;
    if(c == '+') return 62;
    if(c == '/') return 63;
    return -1;
}

KmB64Result km_base64_decode(
    const char* in,
    size_t in_len,
    uint8_t* out,
    size_t out_cap,
    size_t* out_len) {
    *out_len = 0;

    if(in_len % 4 != 0) return KmB64BadLength;

    size_t written = 0;

    for(size_t i = 0; i < in_len; i += 4) {
        int vals[4];
        size_t pad = 0;

        for(size_t j = 0; j < 4; j++) {
            char c = in[i + j];
            if(c == '=') {
                /* Padding is only legal in the last group, last two slots. */
                if(i + 4 != in_len || j < 2) return KmB64BadChar;
                pad++;
                vals[j] = 0;
            } else {
                if(pad > 0) return KmB64BadChar; /* data after padding */
                int v = b64_value(c);
                if(v < 0) return KmB64BadChar;
                vals[j] = v;
            }
        }

        uint32_t triple = ((uint32_t)vals[0] << 18) | ((uint32_t)vals[1] << 12) |
                          ((uint32_t)vals[2] << 6) | (uint32_t)vals[3];

        size_t produce = 3 - pad;
        if(written + produce > out_cap) {
            *out_len = 0;
            return KmB64TooLong;
        }

        if(produce > 0) out[written++] = (uint8_t)((triple >> 16) & 0xFF);
        if(produce > 1) out[written++] = (uint8_t)((triple >> 8) & 0xFF);
        if(produce > 2) out[written++] = (uint8_t)(triple & 0xFF);
    }

    *out_len = written;
    return KmB64Ok;
}
