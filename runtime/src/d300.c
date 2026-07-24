#include "../include/d300.h"

static uint32_t read_u32le(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static int valid_range(size_t total, uint32_t offset, uint32_t length)
{
    if (length == 0) {
        return 1;
    }
    return offset <= total && length <= total - offset;
}

d300_status_t d300_parse(d300_image_t *out, const void *bytes, size_t size)
{
    const uint8_t *data = (const uint8_t *)bytes;

    if (!out || !data || size < D300_MIN_HEADER_SIZE) {
        return D300_ERR_TOO_SHORT;
    }
    if (data[0] != 'D' || data[1] != '3' ||
        data[2] != '0' || data[3] != '0') {
        return D300_ERR_MAGIC;
    }

    out->bytes = data;
    out->size = size;
    out->header_size = read_u32le(data + 0x04);
    out->declared_size = read_u32le(data + 0x08);
    out->flags = read_u32le(data + 0x14);
    out->signature = read_u32le(data + 0x50);
    out->icon_offset = read_u32le(data + 0x88);
    out->icon_size = read_u32le(data + 0x8c);
    out->program_offset = read_u32le(data + 0x98);
    out->program_size = read_u32le(data + 0x9c);
    out->resource_offset = read_u32le(data + 0xa0);
    out->resource_size = read_u32le(data + 0xa4);

    /*
     * Some larger 9288S applications (notably 三国霸业) append their
     * private asset archive after the declared D300 image.  The loader must
     * validate the executable segments against the declared core size while
     * retaining the physical file size so guest file I/O can reach the tail.
     */
    if (out->declared_size > size) {
        return D300_ERR_DECLARED_SIZE;
    }
    if (out->header_size < 0x80 ||
        out->header_size > out->declared_size) {
        return D300_ERR_HEADER;
    }
    if (!valid_range(out->declared_size, out->icon_offset, out->icon_size) ||
        !valid_range(out->declared_size, out->program_offset, out->program_size) ||
        !valid_range(out->declared_size, out->resource_offset, out->resource_size)) {
        return D300_ERR_RANGE;
    }
    return D300_OK;
}

const uint8_t *d300_program(const d300_image_t *image)
{
    return image && image->program_size
        ? image->bytes + image->program_offset : 0;
}

const uint8_t *d300_icon(const d300_image_t *image)
{
    return image && image->icon_size
        ? image->bytes + image->icon_offset : 0;
}

const uint8_t *d300_resource(const d300_image_t *image)
{
    return image && image->resource_size
        ? image->bytes + image->resource_offset : 0;
}

const char *d300_status_string(d300_status_t status)
{
    switch (status) {
    case D300_OK: return "ok";
    case D300_ERR_TOO_SHORT: return "too short";
    case D300_ERR_MAGIC: return "bad magic";
    case D300_ERR_DECLARED_SIZE: return "declared size exceeds file";
    case D300_ERR_HEADER: return "invalid header";
    case D300_ERR_RANGE: return "segment outside declared image";
    default: return "unknown";
    }
}
