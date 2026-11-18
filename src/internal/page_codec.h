#ifndef DEATHBOOBS_INTERNAL_PAGE_CODEC_H
#define DEATHBOOBS_INTERNAL_PAGE_CODEC_H

#include <deathboobs/format.h>

#define DB_PAGE_CRC_OFFSET ((usize)24)
#define DB_PAGE_RESERVED_OFFSET ((usize)28)

static inline void put_u16_le(u8 *out, u16 value)
{
    out[0] = (u8)value;
    out[1] = (u8)(value >> 8);
}

static inline void put_u32_le(u8 *out, u32 value)
{
    out[0] = (u8)value;
    out[1] = (u8)(value >> 8);
    out[2] = (u8)(value >> 16);
    out[3] = (u8)(value >> 24);
}

static inline void put_u64_le(u8 *out, u64 value)
{
    usize i;
    for (i = 0; i < 8; ++i) {
        out[i] = (u8)(value >> (i * 8));
    }
}

static inline u16 get_u16_le(const u8 *in)
{
    return (u16)((u16)in[0] | ((u16)in[1] << 8));
}

static inline u32 get_u32_le(const u8 *in)
{
    return (u32)in[0] | ((u32)in[1] << 8) | ((u32)in[2] << 16) |
           ((u32)in[3] << 24);
}

static inline u64 get_u64_le(const u8 *in)
{
    u64 value = 0;
    usize i;
    for (i = 0; i < 8; ++i) {
        value |= (u64)in[i] << (i * 8);
    }
    return value;
}

static inline bool bytes_are_zero(const u8 *data, usize size)
{
    usize i;
    for (i = 0; i < size; ++i) {
        if (data[i] != 0) {
            return false;
        }
    }
    return true;
}

static inline u32 crc32(const u8 *data, usize size)
{
    u32 crc = 0xffffffffU;
    usize i;
    u8 bit;
    for (i = 0; i < size; ++i) {
        crc ^= data[i];
        for (bit = 0; bit < 8; ++bit) {
            u32 mask = (u32)-(i32)(crc & 1U);
            crc = (crc >> 1) ^ (0xedb88320U & mask);
        }
    }
    return ~crc;
}

static inline bool page_type_valid(DeathboobsPageType type)
{
    return type >= DEATHBOOBS_PAGE_TYPE_METADATA &&
           type <= DEATHBOOBS_PAGE_TYPE_INDEX_RESERVED;
}

static inline ErrorCode encode_page(const DeathboobsPage *page, u8 *out)
{
    usize i;
    if (!page || !page_type_valid(page->type) ||
        page->payload_size > DEATHBOOBS_PAGE_PAYLOAD_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }
    for (i = 0; i < DEATHBOOBS_PAGE_SIZE; ++i) {
        out[i] = 0;
    }
    put_u64_le(out, page->id);
    put_u16_le(out + 8, (u16)page->type);
    put_u16_le(out + 10, page->flags);
    put_u32_le(out + 12, page->payload_size);
    put_u64_le(out + 16, page->generation);
    for (i = 0; i < page->payload_size; ++i) {
        out[DEATHBOOBS_PAGE_HEADER_SIZE + i] = page->payload[i];
    }
    put_u32_le(out + DB_PAGE_CRC_OFFSET, 0);
    put_u32_le(out + DB_PAGE_CRC_OFFSET, crc32(out, DEATHBOOBS_PAGE_SIZE));
    return CORE_OK;
}

static inline ErrorCode decode_page(u8 *input, DeathboobsPageId expected_id,
                             DeathboobsPage *out)
{
    DeathboobsPage page;
    u16 raw_type;
    u32 stored_crc;
    u32 actual_crc;
    usize i;
    if (!out) {
        return CORE_INVALID_ARGUMENT;
    }
    if (get_u64_le(input) != expected_id) {
        return CORE_IO_ERROR;
    }
    raw_type = get_u16_le(input + 8);
    if (raw_type < DEATHBOOBS_PAGE_TYPE_METADATA ||
        raw_type > DEATHBOOBS_PAGE_TYPE_INDEX_RESERVED) {
        return CORE_IO_ERROR;
    }
    page.id = expected_id;
    page.type = (DeathboobsPageType)raw_type;
    page.flags = get_u16_le(input + 10);
    page.payload_size = get_u32_le(input + 12);
    page.generation = get_u64_le(input + 16);
    if (page.payload_size > DEATHBOOBS_PAGE_PAYLOAD_SIZE ||
        !bytes_are_zero(input + DB_PAGE_RESERVED_OFFSET, 4) ||
        !bytes_are_zero(input + DEATHBOOBS_PAGE_HEADER_SIZE + page.payload_size,
                        DEATHBOOBS_PAGE_PAYLOAD_SIZE - page.payload_size)) {
        return CORE_IO_ERROR;
    }
    stored_crc = get_u32_le(input + DB_PAGE_CRC_OFFSET);
    put_u32_le(input + DB_PAGE_CRC_OFFSET, 0);
    actual_crc = crc32(input, DEATHBOOBS_PAGE_SIZE);
    put_u32_le(input + DB_PAGE_CRC_OFFSET, stored_crc);
    if (stored_crc != actual_crc) {
        return CORE_IO_ERROR;
    }
    for (i = 0; i < DEATHBOOBS_PAGE_PAYLOAD_SIZE; ++i) {
        page.payload[i] = i < page.payload_size
                              ? input[DEATHBOOBS_PAGE_HEADER_SIZE + i]
                              : 0;
    }
    *out = page;
    return CORE_OK;
}

#endif
