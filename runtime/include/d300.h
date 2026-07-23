#ifndef BBK9288S_D300_H
#define BBK9288S_D300_H

#include <stddef.h>
#include <stdint.h>

#define D300_MIN_HEADER_SIZE 0xC0u
#define D300_GUEST_LOAD_BASE 0x02700000u

typedef enum d300_status {
    D300_OK = 0,
    D300_ERR_TOO_SHORT,
    D300_ERR_MAGIC,
    D300_ERR_DECLARED_SIZE,
    D300_ERR_HEADER,
    D300_ERR_RANGE
} d300_status_t;

typedef struct d300_image {
    const uint8_t *bytes;
    size_t size;
    uint32_t header_size;
    uint32_t declared_size;
    uint32_t flags;
    uint32_t signature;
    uint32_t icon_offset;
    uint32_t icon_size;
    uint32_t program_offset;
    uint32_t program_size;
    uint32_t resource_offset;
    uint32_t resource_size;
} d300_image_t;

d300_status_t d300_parse(d300_image_t *out, const void *bytes, size_t size);
const uint8_t *d300_program(const d300_image_t *image);
const uint8_t *d300_icon(const d300_image_t *image);
const uint8_t *d300_resource(const d300_image_t *image);
const char *d300_status_string(d300_status_t status);

#endif
