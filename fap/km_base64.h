#pragma once
#include <stddef.h>
#include <stdint.h>

typedef enum {
    KmB64Ok = 0,
    KmB64BadChar,
    KmB64BadLength,
    KmB64TooLong,
} KmB64Result;

/**
 * Decode standard base64. Requires in_len to be a multiple of 4.
 * Writes at most out_cap bytes; returns KmB64TooLong if the decoded
 * form would exceed out_cap. On any error *out_len is set to 0.
 */
KmB64Result km_base64_decode(
    const char* in,
    size_t in_len,
    uint8_t* out,
    size_t out_cap,
    size_t* out_len);
