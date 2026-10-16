#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CHUNK_TYPE_SIZE 4

/**
 * @brief Write a 32-bit unsigned integer to a file in big-endian order.
 *
 * @param file  -- The file pointer to write to.
 * @param value -- The 32-bit unsigned integer value to write.
 *
 * @return true  -- If the write operation was successful.
 * @return false -- If the write operation failed.
 */
static bool write_u32(FILE* file, uint32_t value)
{
    const uint8_t bytes[] = {
        (uint8_t)(value >> 24),
        (uint8_t)(value >> 16),
        (uint8_t)(value >> 8),
        (uint8_t)value,
    };

    return fwrite(bytes, 1, sizeof(bytes), file) == sizeof(bytes);
}

static uint32_t update_crc32(uint32_t crc, const uint8_t* data, size_t length)
{
    size_t i;
    int    bit;

    for (i = 0; i < length; ++i) {
        crc ^= data[i];
        for (bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xedb88320u & (uint32_t) - (int)(crc & 1));
        }
    }

    return crc;
}

/**
 * @brief Write a PNG chunk to a file.
 *
 * @param file   -- The file pointer to write to.
 * @param type   -- The 4-character type of the PNG chunk.
 * @param data   -- The data to write in the chunk.
 * @param length -- The length of the data in bytes.
 *
 * @return true  -- If the write operation was successful.
 * @return false -- If the write operation failed.
 */
static bool write_chunk(FILE* file, const char chunk_type[4], const uint8_t* data, uint32_t length)
{
    uint32_t crc;

    if (!write_u32(file, length)) {
        return false;
    }

    if (fwrite(chunk_type, 1, CHUNK_TYPE_SIZE, file) != CHUNK_TYPE_SIZE) {
        return false;
    }

    crc = update_crc32(0xffffffffu, (const uint8_t*)chunk_type, CHUNK_TYPE_SIZE);

    if (length != 0) {
        if (fwrite(data, 1, length, file) != length) {
            return false;
        }

        crc = update_crc32(crc, data, length);
    }

    if (!write_u32(file, crc ^ 0xffffffffu)) {
        return false;
    }

    return true;
}

const uint32_t MOD_ADLER = 65521;

uint32_t adler32(uint8_t* data, size_t len)
{
    uint32_t a = 1, b = 0;
    size_t   index;

    // Process each byte of the data in order
    for (index = 0; index < len; ++index) {
        a = (a + data[index]) % MOD_ADLER;
        b = (b + a) % MOD_ADLER;
    }

    return (b << 16) | a;
}

void idat_put_pixels(uint8_t* buffer, uint8_t num_of_pixels, uint8_t r, uint8_t g, uint8_t b)
{
    for (uint16_t i = 0; i < num_of_pixels * 3; i += 3) {
        buffer[i]     = r;
        buffer[i + 1] = g;
        buffer[i + 2] = b;
    }
}

int main(int argc, char** argv)
{
    // clang-format off
    static const uint8_t signature[] = {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};
    static const uint8_t ihdr[13]    = {
        0, 0, 0, 100, // width
        0, 0, 0, 100, // height
        8,            // bit depth: 8 bit
        2,            // color type: RGB
        0,            // compression: none
        0,            // filter: none
        0             // interlace: none
    };
    static uint8_t idat[101*100*3 + 7 + 4] = {
        0x78, 0x01,                     // zlib header (DEFLATE with 32 kB window)
        0x01,                           // final, uncompressed DEFLATE block
                  0x94,           0x75, // block data length: 100*101, little-endian
        (uint8_t)~0x94, (uint8_t)~0x75, // one's complement from the values above
    };
    // clang-format on

    uint32_t idx = 7;

    for (uint8_t row = 0; row < 50; row++) {
        idat[idx] = 0x00; // filter byte
        ++idx;
        idat_put_pixels(&idat[idx], 50, 0xaa, 0x00, 0x00);
        idx += (3 * 50);
        idat_put_pixels(&idat[idx], 50, 0x00, 0xaa, 0x00);
        idx += (3 * 50);
    }

    for (uint8_t row = 0; row < 50; row++) {
        idat[idx] = 0x00; // filter byte
        ++idx;
        idat_put_pixels(&idat[idx], 50, 0x00, 0x00, 0xaa);
        idx += (3 * 50);
        idat_put_pixels(&idat[idx], 50, 0xaa, 0xaa, 0xaa);
        idx += (3 * 50);
    }

    uint32_t adler32_csum = adler32(&idat[7], 100 * 101);

    idat[sizeof(idat) - 4] = (uint8_t)(adler32_csum >> 24);
    idat[sizeof(idat) - 3] = (uint8_t)(adler32_csum >> 16);
    idat[sizeof(idat) - 2] = (uint8_t)(adler32_csum >> 8);
    idat[sizeof(idat) - 1] = (uint8_t)adler32_csum;

    static const uint8_t empty[] = {0};
    FILE*                file;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <output.png>\n", argv[0]);
        return EXIT_FAILURE;
    }

    file = fopen(argv[1], "wb");
    if (file == NULL) {
        perror(argv[1]);
        return EXIT_FAILURE;
    }

    if (fwrite(signature, 1, sizeof(signature), file) != sizeof(signature)) {
        fclose(file);
        fprintf(stderr, "Failed to write PNG: %s\n", argv[1]);
        return EXIT_FAILURE;
    }

    if (!write_chunk(file, "IHDR", ihdr, sizeof(ihdr))) {
        fclose(file);
        fprintf(stderr, "Failed to write PNG: %s\n", argv[1]);
        return EXIT_FAILURE;
    }

    if (!write_chunk(file, "IDAT", idat, sizeof(idat))) {
        fclose(file);
        fprintf(stderr, "Failed to write PNG: %s\n", argv[1]);
        return EXIT_FAILURE;
    }

    if (!write_chunk(file, "IEND", empty, 0)) {
        fclose(file);
        fprintf(stderr, "Failed to write PNG: %s\n", argv[1]);
        return EXIT_FAILURE;
    }

    if (fclose(file)) {
        fprintf(stderr, "Failed to close PNG: %s\n", argv[1]);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
