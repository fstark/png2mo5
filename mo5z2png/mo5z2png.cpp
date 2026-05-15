// mo5z2png — Convert a .mo5z file back to a 320×200 PNG
// Single-file C++23 implementation. Quick and dirty decoder.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>
#include <stdexcept>

namespace fs = std::filesystem;

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

// ─── Constants ───────────────────────────────────────────────────────────────

constexpr int MO5_W         = 320;
constexpr int MO5_H         = 200;
constexpr int COLS           = MO5_W / 8;   // 40
constexpr int ROWS           = MO5_H;       // 200
constexpr int TOTAL_BLOCKS   = COLS * ROWS;  // 8000
constexpr int PACKED_NIBBLES = TOTAL_BLOCKS / 2; // 4000
constexpr int INITIAL_OFFSET = 1;

// MO5 fixed 16-color palette (sRGB)
constexpr uint8_t PALETTE[16][3] = {
    {  0,   0,   0}, // 0  Black
    {255,   0,   0}, // 1  Red
    {  0, 255,   0}, // 2  Green
    {255, 255,   0}, // 3  Yellow
    {  0,   0, 255}, // 4  Blue
    {255,   0, 255}, // 5  Magenta
    {  0, 255, 255}, // 6  Cyan
    {255, 255, 255}, // 7  White
    {128, 128, 128}, // 8  Gray
    {255, 128, 128}, // 9  Pink
    {128, 255, 128}, // 10 Light Green
    {255, 255, 128}, // 11 Light Yellow
    {128, 128, 255}, // 12 Light Blue
    {255, 128, 255}, // 13 Purple
    {128, 255, 255}, // 14 Light Cyan
    {255, 128,   0}, // 15 Orange
};

// ─── ZX0 v2 Decompression ───────────────────────────────────────────────────

// Decompress one ZX0 v2 stream starting at src[0..src_size).
// Returns the decompressed bytes. Sets *bytes_consumed to the number of
// input bytes read, so the caller can locate the next concatenated stream.
std::vector<uint8_t> zx0_decompress(const uint8_t* src, size_t src_size,
                                     size_t expected_size, size_t* bytes_consumed) {
    std::vector<uint8_t> out;
    out.reserve(expected_size);

    size_t src_idx = 0;
    int bit_mask = 0;
    int bit_value = 0;
    int backtrack = 0;
    int last_byte = 0;
    int last_offset = INITIAL_OFFSET;

    auto read_byte = [&]() -> int {
        if (src_idx >= src_size) throw std::runtime_error("zx0: unexpected end of input");
        last_byte = src[src_idx++];
        return last_byte;
    };

    auto read_bit = [&]() -> int {
        if (backtrack) {
            backtrack = 0;
            return last_byte & 1;
        }
        bit_mask >>= 1;
        if (bit_mask == 0) {
            bit_mask = 128;
            bit_value = read_byte();
        }
        return (bit_value & bit_mask) ? 1 : 0;
    };

    auto read_elias = [&](bool inverted) -> int {
        int value = 1;
        while (!read_bit()) {
            value = (value << 1) | (read_bit() ^ (inverted ? 1 : 0));
        }
        return value;
    };

    auto copy_bytes = [&](int offset, int length) {
        for (int i = 0; i < length; i++) {
            size_t idx = out.size() - offset;
            out.push_back(out[idx]);
        }
    };

    enum State { COPY_LITERALS, COPY_FROM_LAST, COPY_FROM_NEW, DONE };
    State state = COPY_LITERALS;

    while (state != DONE) {
        switch (state) {
        case COPY_LITERALS: {
            int length = read_elias(false);
            for (int i = 0; i < length; i++) out.push_back(static_cast<uint8_t>(read_byte()));
            state = read_bit() ? COPY_FROM_NEW : COPY_FROM_LAST;
            break;
        }
        case COPY_FROM_LAST: {
            int length = read_elias(false);
            copy_bytes(last_offset, length);
            state = read_bit() ? COPY_FROM_NEW : COPY_LITERALS;
            break;
        }
        case COPY_FROM_NEW: {
            int msb = read_elias(true);
            if (msb == 256) { state = DONE; break; }
            last_offset = msb * 128 - (read_byte() >> 1);
            backtrack = 1;
            int length = read_elias(false) + 1;
            copy_bytes(last_offset, length);
            state = read_bit() ? COPY_FROM_NEW : COPY_LITERALS;
            break;
        }
        case DONE: break;
        }
    }

    if (out.size() != expected_size)
        throw std::runtime_error("zx0: size mismatch (got " +
            std::to_string(out.size()) + ", expected " + std::to_string(expected_size) + ")");

    if (bytes_consumed) *bytes_consumed = src_idx;
    return out;
}

// ─── Reorder & Unpack ────────────────────────────────────────────────────────

// Column-major to row-major transpose for an 8000-byte buffer.
std::vector<uint8_t> reorder_col_to_row(const std::vector<uint8_t>& col_major) {
    std::vector<uint8_t> row_major(TOTAL_BLOCKS);
    for (int col = 0; col < COLS; col++)
        for (int row = 0; row < ROWS; row++)
            row_major[row * COLS + col] = col_major[col * ROWS + row];
    return row_major;
}

// Unpack nibble pairs: each byte → two 4-bit values.
std::vector<uint8_t> unpack_nibble_pairs(const std::vector<uint8_t>& packed) {
    std::vector<uint8_t> out(packed.size() * 2);
    for (size_t i = 0; i < packed.size(); i++) {
        out[2 * i]     = (packed[i] >> 4) & 0x0F;
        out[2 * i + 1] =  packed[i] & 0x0F;
    }
    return out;
}

// ─── Rendering ───────────────────────────────────────────────────────────────

// Render 8000 blocks (row-major pixels, fg, bg) into a 320×200 RGB buffer.
std::vector<uint8_t> render(const std::vector<uint8_t>& pixels,
                            const std::vector<uint8_t>& fg,
                            const std::vector<uint8_t>& bg) {
    std::vector<uint8_t> buf(MO5_W * MO5_H * 3);
    for (int i = 0; i < TOTAL_BLOCKS; i++) {
        int y  = i / COLS;
        int x0 = (i % COLS) * 8;
        for (int bit = 7; bit >= 0; --bit) {
            uint8_t color_idx = ((pixels[i] >> bit) & 1) ? fg[i] : bg[i];
            int px = x0 + (7 - bit);
            int off = (y * MO5_W + px) * 3;
            buf[off + 0] = PALETTE[color_idx][0];
            buf[off + 1] = PALETTE[color_idx][1];
            buf[off + 2] = PALETTE[color_idx][2];
        }
    }
    return buf;
}

// ─── File I/O ────────────────────────────────────────────────────────────────

std::vector<uint8_t> read_file(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) throw std::runtime_error("Cannot open: " + path);
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> data(size);
    if (fread(data.data(), 1, size, f) != static_cast<size_t>(size)) {
        fclose(f);
        throw std::runtime_error("Read failed: " + path);
    }
    fclose(f);
    return data;
}

// ─── Main ────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    if (argc < 2 || argc > 3) {
        std::fprintf(stderr, "Usage: mo5z2png <input.mo5z> [output.png]\n");
        return 1;
    }

    std::string input_path = argv[1];
    std::string output_path;
    if (argc == 3) {
        output_path = argv[2];
    } else {
        fs::path p(input_path);
        p.replace_extension(".png");
        output_path = p.string();
    }

    try {
        auto data = read_file(input_path);
        const uint8_t* ptr = data.data();
        size_t remaining = data.size();

        // Decompress three concatenated ZX0 streams
        size_t consumed = 0;

        auto pixels_col = zx0_decompress(ptr, remaining, TOTAL_BLOCKS, &consumed);
        ptr += consumed; remaining -= consumed;

        auto fg_packed = zx0_decompress(ptr, remaining, PACKED_NIBBLES, &consumed);
        ptr += consumed; remaining -= consumed;

        auto bg_packed = zx0_decompress(ptr, remaining, PACKED_NIBBLES, &consumed);

        // Unpack nibble pairs → 8000 each
        auto fg_nibbles = unpack_nibble_pairs(fg_packed);
        auto bg_nibbles = unpack_nibble_pairs(bg_packed);

        // Column-major → row-major
        auto pixels = reorder_col_to_row(pixels_col);
        auto fg     = reorder_col_to_row(fg_nibbles);
        auto bg     = reorder_col_to_row(bg_nibbles);

        // Render to RGB and write PNG
        auto rgb = render(pixels, fg, bg);
        if (!stbi_write_png(output_path.c_str(), MO5_W, MO5_H, 3, rgb.data(), MO5_W * 3)) {
            std::fprintf(stderr, "Error: cannot write '%s'\n", output_path.c_str());
            return 1;
        }

        std::printf("wrote %s\n", output_path.c_str());
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Error: %s\n", e.what());
        return 1;
    }
}
