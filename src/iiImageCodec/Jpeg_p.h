#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
struct ii_jpeg_result {
    unsigned char *bytes;
    size_t size;
    unsigned width, height;
    int limit;
    char error[200];
};
struct ii_jpeg_result ii_jpeg_decode(const unsigned char *, size_t, unsigned, size_t, int);
struct ii_jpeg_result ii_jpeg_encode(const unsigned char *, unsigned, unsigned, size_t, int, size_t);
#ifdef __cplusplus
}
#endif
