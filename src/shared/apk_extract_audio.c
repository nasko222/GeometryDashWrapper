#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "loader.h"
#include "runtime.h"
#include "zlib.h"

/*
 * Audio-only APK extraction helper.
 * Kept separate from loader.c so the Dynarmic host does not pull in the
 * legacy x86 ELF relocation/runtime thunk dependencies.
 */

static int range_valid(size_t offset, size_t length, size_t total) {
    return offset <= total && length <= total - offset;
}

static int read_entire_file(const char *path, unsigned char **data, size_t *size) {
    FILE *stream = fopen(path, "rb");
    long length;
    unsigned char *buffer;
    if (!stream) {
        runtime_log("ERROR: cannot open %s", path);
        return 0;
    }
    if (fseek(stream, 0, SEEK_END) != 0 || (length = ftell(stream)) <= 0 ||
        fseek(stream, 0, SEEK_SET) != 0) {
        fclose(stream);
        runtime_log("ERROR: cannot determine ELF file size");
        return 0;
    }
    buffer = (unsigned char *)malloc((size_t)length);
    if (!buffer || fread(buffer, 1, (size_t)length, stream) != (size_t)length) {
        free(buffer);
        fclose(stream);
        runtime_log("ERROR: cannot read complete ELF file");
        return 0;
    }
    fclose(stream);
    *data = buffer;
    *size = (size_t)length;
    return 1;
}

static uint16_t read_u16(const unsigned char *value) {
    return (uint16_t)((uint16_t)value[0] | ((uint16_t)value[1] << 8));
}

static uint32_t read_u32(const unsigned char *value) {
    return (uint32_t)value[0] | ((uint32_t)value[1] << 8) |
           ((uint32_t)value[2] << 16) | ((uint32_t)value[3] << 24);
}

static int apk_extract_member_internal(const char *apk_path,
                                       const char *member_name,
                                       unsigned char **output,
                                       size_t *output_size,
                                       int report_missing) {
    unsigned char *apk = NULL;
    size_t apk_size = 0;
    size_t eocd_position;
    size_t search_limit;
    size_t central_position;
    uint16_t entry_count;
    uint16_t entry_index;
    size_t member_length = strlen(member_name);
    int found = 0;

    if (!read_entire_file(apk_path, &apk, &apk_size) || apk_size < 22) {
        free(apk);
        return 0;
    }
    eocd_position = apk_size - 22;
    search_limit = eocd_position > 0xffffu ? eocd_position - 0xffffu : 0;
    for (;;) {
        if (read_u32(apk + eocd_position) == 0x06054b50u &&
            range_valid(eocd_position, 22, apk_size) &&
            range_valid(eocd_position + 22,
                        read_u16(apk + eocd_position + 20), apk_size)) {
            found = 1;
            break;
        }
        if (eocd_position == search_limit) {
            break;
        }
        --eocd_position;
    }
    if (!found || read_u16(apk + eocd_position + 4) != 0 ||
        read_u16(apk + eocd_position + 6) != 0) {
        runtime_log("ERROR: APK has no supported ZIP central directory");
        free(apk);
        return 0;
    }
    entry_count = read_u16(apk + eocd_position + 10);
    central_position = read_u32(apk + eocd_position + 16);
    if (!range_valid(central_position,
                     read_u32(apk + eocd_position + 12), apk_size)) {
        runtime_log("ERROR: APK central directory is outside the file");
        free(apk);
        return 0;
    }

    for (entry_index = 0; entry_index < entry_count; ++entry_index) {
        uint16_t name_length;
        uint16_t extra_length;
        uint16_t comment_length;
        size_t entry_size;
        uint16_t flags;
        uint16_t method;
        uint32_t expected_crc;
        uint32_t compressed_size;
        uint32_t uncompressed_size;
        uint32_t local_offset;
        size_t data_offset;
        unsigned char *payload;

        if (!range_valid(central_position, 46, apk_size) ||
            read_u32(apk + central_position) != 0x02014b50u) {
            runtime_log("ERROR: malformed APK central-directory entry");
            break;
        }
        name_length = read_u16(apk + central_position + 28);
        extra_length = read_u16(apk + central_position + 30);
        comment_length = read_u16(apk + central_position + 32);
        entry_size = 46u + name_length + extra_length + comment_length;
        if (!range_valid(central_position, entry_size, apk_size)) {
            runtime_log("ERROR: truncated APK central-directory entry");
            break;
        }
        if (name_length != member_length ||
            memcmp(apk + central_position + 46, member_name, member_length) != 0) {
            central_position += entry_size;
            continue;
        }

        flags = read_u16(apk + central_position + 8);
        method = read_u16(apk + central_position + 10);
        expected_crc = read_u32(apk + central_position + 16);
        compressed_size = read_u32(apk + central_position + 20);
        uncompressed_size = read_u32(apk + central_position + 24);
        local_offset = read_u32(apk + central_position + 42);
        if ((flags & 1u) != 0 || (method != 0 && method != 8) ||
            !uncompressed_size || !range_valid(local_offset, 30, apk_size) ||
            read_u32(apk + local_offset) != 0x04034b50u) {
            runtime_log("ERROR: unsupported or malformed APK member: %s",
                        member_name);
            break;
        }
        data_offset = (size_t)local_offset + 30u +
                      read_u16(apk + local_offset + 26) +
                      read_u16(apk + local_offset + 28);
        if (!range_valid(data_offset, compressed_size, apk_size)) {
            runtime_log("ERROR: APK member data is truncated: %s", member_name);
            break;
        }
        payload = (unsigned char *)malloc(uncompressed_size);
        if (!payload) {
            runtime_log("ERROR: cannot allocate %lu bytes for APK member",
                        (unsigned long)uncompressed_size);
            break;
        }
        if (method == 0) {
            if (compressed_size != uncompressed_size) {
                free(payload);
                runtime_log("ERROR: invalid stored APK member size");
                break;
            }
            memcpy(payload, apk + data_offset, uncompressed_size);
        } else {
            z_stream stream;
            int inflate_result;
            memset(&stream, 0, sizeof(stream));
            stream.next_in = apk + data_offset;
            stream.avail_in = compressed_size;
            stream.next_out = payload;
            stream.avail_out = uncompressed_size;
            inflate_result = inflateInit2(&stream, -MAX_WBITS);
            if (inflate_result == Z_OK) {
                inflate_result = inflate(&stream, Z_FINISH);
                inflateEnd(&stream);
            }
            if (inflate_result != Z_STREAM_END ||
                stream.total_out != uncompressed_size) {
                free(payload);
                runtime_log("ERROR: cannot inflate APK member: %s", member_name);
                break;
            }
        }
        if ((uint32_t)crc32(crc32(0L, Z_NULL, 0), payload,
                            uncompressed_size) != expected_crc) {
            free(payload);
            runtime_log("ERROR: APK member CRC mismatch: %s", member_name);
            break;
        }
        *output = payload;
        *output_size = uncompressed_size;
        runtime_log("Loaded %s directly from APK (%lu bytes)", member_name,
                    (unsigned long)uncompressed_size);
        free(apk);
        return 1;
    }
    if (report_missing) {
        runtime_log("ERROR: APK does not contain a usable %s", member_name);
    }
    free(apk);
    return 0;
}

int apk_extract_member(const char *apk_path, const char *member_name,
                       unsigned char **output, size_t *output_size) {
    return apk_extract_member_internal(apk_path, member_name, output,
                                       output_size, 1);
}
