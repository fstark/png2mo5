#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <array>
#include <string>
#include <vector>
#include <stdexcept>

extern "C" {
#include "zx0/zx0.h"
}

// ─── Constants ───────────────────────────────────────────────────────────────

constexpr int MO5_W        = 320;
constexpr int MO5_H        = 200;
constexpr int COLS          = MO5_W / 8;  // 40
constexpr int ROWS          = MO5_H;      // 200
constexpr int TOTAL_BLOCKS  = COLS * ROWS; // 8000

// ─── Data Structures ─────────────────────────────────────────────────────────

struct Options {
    std::string pixels_path;
    std::string colors_path;
    std::string output_stem = "out";
};

struct Streams {
    std::vector<uint8_t> pixels;    // 8000 bytes, column-major
    std::vector<uint8_t> fg_packed; // 4000 bytes, nibble-pair packed
    std::vector<uint8_t> bg_packed; // 4000 bytes, nibble-pair packed
};

struct CompressedStreams {
    std::vector<uint8_t> pixels_zx0;
    std::vector<uint8_t> fg_zx0;
    std::vector<uint8_t> bg_zx0;
};

struct NibbleChoice {
    uint8_t pix;
    uint8_t n1;
    uint8_t n2;
};

// ─── Reorder Functions ───────────────────────────────────────────────────────

std::vector<uint8_t> reorder_col_to_row(const std::vector<uint8_t>& col_major) {
    std::vector<uint8_t> row_major(8000);
    for (int col = 0; col < COLS; col++)
        for (int row = 0; row < ROWS; row++)
            row_major[row * COLS + col] = col_major[col * ROWS + row];
    return row_major;
}

std::vector<uint8_t> reorder_row_to_col(const std::vector<uint8_t>& row_major) {
    std::vector<uint8_t> col_major(8000);
    for (int col = 0; col < COLS; col++)
        for (int row = 0; row < ROWS; row++)
            col_major[col * ROWS + row] = row_major[row * COLS + col];
    return col_major;
}

// ─── Nibble Packing ──────────────────────────────────────────────────────────

std::vector<uint8_t> pack_nibble_pairs(const std::vector<uint8_t>& nibbles) {
    std::vector<uint8_t> packed(nibbles.size() / 2);
    for (size_t i = 0; i < packed.size(); i++) {
        packed[i] = (nibbles[2*i] << 4) | (nibbles[2*i + 1] & 0x0F);
    }
    return packed;
}

std::vector<uint8_t> unpack_nibble_pairs(const std::vector<uint8_t>& packed) {
    std::vector<uint8_t> out(packed.size() * 2);
    for (size_t i = 0; i < packed.size(); i++) {
        out[2*i]     = (packed[i] >> 4) & 0x0F;
        out[2*i + 1] = packed[i] & 0x0F;
    }
    return out;
}

// ─── Free Nibble Logic ───────────────────────────────────────────────────────

NibbleChoice solve_free_nibble(uint8_t visible, uint8_t prev_n1, uint8_t prev_n2) {
    bool match1 = (visible == prev_n1);
    bool match2 = (visible == prev_n2);

    if (match1 && !match2) {
        // Place visible in stream 1 → pixels = 0xFF, n2 = prev_n2
        return {0xFF, visible, prev_n2};
    }
    // All other cases: place visible in stream 2 → pixels = 0x00, n1 = prev_n1
    return {0x00, prev_n1, visible};
}

// ─── Visual Equivalence ──────────────────────────────────────────────────────

bool blocks_visually_equal(uint8_t pix_a, uint8_t fg_a, uint8_t bg_a,
                           uint8_t pix_b, uint8_t fg_b, uint8_t bg_b) {
    for (int bit = 7; bit >= 0; bit--) {
        uint8_t color_a = (pix_a & (1 << bit)) ? fg_a : bg_a;
        uint8_t color_b = (pix_b & (1 << bit)) ? fg_b : bg_b;
        if (color_a != color_b) return false;
    }
    return true;
}

// ─── Canonicalization ────────────────────────────────────────────────────────

void canonicalize_and_reorder(
    const uint8_t* pixels_row_major,
    const uint8_t* colors_row_major,
    std::vector<uint8_t>& out_pixels,
    std::vector<uint8_t>& out_fg,
    std::vector<uint8_t>& out_bg)
{
    out_pixels.resize(TOTAL_BLOCKS);
    out_fg.resize(TOTAL_BLOCKS);
    out_bg.resize(TOTAL_BLOCKS);

    for (int col = 0; col < COLS; col++) {
        uint8_t prev_n1 = 0xFF; // sentinel
        uint8_t prev_n2 = 0xFF;

        for (int row = 0; row < ROWS; row++) {
            int row_major_idx = row * COLS + col;
            int col_major_idx = col * ROWS + row;

            uint8_t pix = pixels_row_major[row_major_idx];
            uint8_t color = colors_row_major[row_major_idx];
            uint8_t n_a = (color >> 4) & 0x0F;
            uint8_t n_b = color & 0x0F;

            uint8_t chosen_pix, chosen_n1, chosen_n2;

            if (row == 0) {
                // First block in column: deterministic min/max rule
                if (n_a <= n_b) {
                    chosen_n1 = n_a; chosen_n2 = n_b; chosen_pix = pix;
                } else {
                    chosen_n1 = n_b; chosen_n2 = n_a; chosen_pix = pix ^ 0xFF;
                }
            } else if (pix == 0x00) {
                // Solid block: all pixels are bg color (n_b is visible)
                NibbleChoice nc = solve_free_nibble(n_b, prev_n1, prev_n2);
                chosen_pix = nc.pix; chosen_n1 = nc.n1; chosen_n2 = nc.n2;
            } else if (pix == 0xFF) {
                // Solid block: all pixels are fg color (n_a is visible)
                NibbleChoice nc = solve_free_nibble(n_a, prev_n1, prev_n2);
                chosen_pix = nc.pix; chosen_n1 = nc.n1; chosen_n2 = nc.n2;
            } else {
                // Normal block — pick orientation minimizing cost
                int cost_a = (n_a != prev_n1) + (n_b != prev_n2);
                int cost_b = (n_b != prev_n1) + (n_a != prev_n2);
                if (cost_a <= cost_b) {
                    chosen_n1 = n_a; chosen_n2 = n_b; chosen_pix = pix;
                } else {
                    chosen_n1 = n_b; chosen_n2 = n_a; chosen_pix = pix ^ 0xFF;
                }
            }

            out_pixels[col_major_idx] = chosen_pix;
            out_fg[col_major_idx] = chosen_n1;
            out_bg[col_major_idx] = chosen_n2;
            prev_n1 = chosen_n1;
            prev_n2 = chosen_n2;
        }
    }
}

// ─── Build Streams ───────────────────────────────────────────────────────────

Streams build_streams(const uint8_t* pixels, const uint8_t* colors) {
    std::vector<uint8_t> col_pixels, col_fg, col_bg;
    canonicalize_and_reorder(pixels, colors, col_pixels, col_fg, col_bg);

    Streams s;
    s.pixels = std::move(col_pixels);
    s.fg_packed = pack_nibble_pairs(col_fg);
    s.bg_packed = pack_nibble_pairs(col_bg);
    return s;
}

// ─── ZX0 Compression Wrapper ─────────────────────────────────────────────────

std::vector<uint8_t> zx0_compress(const std::vector<uint8_t>& data) {
    int output_size = 0;
    int delta = 0;
    BLOCK* optimal = optimize(
        const_cast<unsigned char*>(data.data()),
        static_cast<int>(data.size()),
        0,
        static_cast<int>(data.size())
    );
    unsigned char* compressed = compress(
        optimal,
        const_cast<unsigned char*>(data.data()),
        static_cast<int>(data.size()),
        0,    // skip
        0,    // backwards_mode = false
        1,    // invert_mode = true (v2 format)
        &output_size,
        &delta
    );
    std::vector<uint8_t> result(compressed, compressed + output_size);
    free(compressed);
    return result;
}

// ─── ZX0 Decompression (in-memory) ──────────────────────────────────────────

std::vector<uint8_t> zx0_decompress(const uint8_t* src, size_t src_size, size_t expected_size) {
    std::vector<uint8_t> out;
    out.reserve(expected_size);

    size_t src_idx = 0;
    int bit_mask_d = 0;
    int bit_value = 0;
    int backtrack_d = 0;
    int last_byte = 0;
    int last_offset = INITIAL_OFFSET;

    auto read_byte = [&]() -> int {
        if (src_idx >= src_size) throw std::runtime_error("zx0 decompress: unexpected end of input");
        last_byte = src[src_idx++];
        return last_byte;
    };

    auto read_bit = [&]() -> int {
        if (backtrack_d) {
            backtrack_d = 0;
            return last_byte & 1;
        }
        bit_mask_d >>= 1;
        if (bit_mask_d == 0) {
            bit_mask_d = 128;
            bit_value = read_byte();
        }
        return (bit_value & bit_mask_d) ? 1 : 0;
    };

    auto read_elias = [&](bool inverted) -> int {
        int value = 1;
        while (!read_bit()) {
            value = (value << 1) | (read_bit() ^ (inverted ? 1 : 0));
        }
        return value;
    };

    auto write_out = [&](uint8_t b) { out.push_back(b); };

    auto copy_bytes = [&](int offset, int length) {
        for (int i = 0; i < length; i++) {
            size_t idx = out.size() - offset;
            write_out(out[idx]);
        }
    };

    // State machine using enum for clarity
    enum State { COPY_LITERALS, COPY_FROM_LAST, COPY_FROM_NEW, DONE };
    State state = COPY_LITERALS;

    while (state != DONE) {
        switch (state) {
        case COPY_LITERALS: {
            int length = read_elias(false);
            for (int i = 0; i < length; i++) write_out(static_cast<uint8_t>(read_byte()));
            if (read_bit()) {
                state = COPY_FROM_NEW;
            } else {
                state = COPY_FROM_LAST;
            }
            break;
        }
        case COPY_FROM_LAST: {
            int length = read_elias(false);
            copy_bytes(last_offset, length);
            if (read_bit()) {
                state = COPY_FROM_NEW;
            } else {
                state = COPY_LITERALS;
            }
            break;
        }
        case COPY_FROM_NEW: {
            int msb = read_elias(true);
            if (msb == 256) { state = DONE; break; }
            last_offset = msb * 128 - (read_byte() >> 1);
            backtrack_d = 1;
            int length = read_elias(false) + 1;
            copy_bytes(last_offset, length);
            if (read_bit()) {
                state = COPY_FROM_NEW;
            } else {
                state = COPY_LITERALS;
            }
            break;
        }
        case DONE:
            break;
        }
    }

    if (out.size() != expected_size)
        throw std::runtime_error("zx0 decompress: size mismatch (got " +
            std::to_string(out.size()) + ", expected " + std::to_string(expected_size) + ")");
    return out;
}

// ─── Verification ────────────────────────────────────────────────────────────

void verify(const Streams& streams, const CompressedStreams& compressed,
            const uint8_t* orig_pixels, const uint8_t* orig_colors) {
    // Decompress
    auto dec_pixels = zx0_decompress(compressed.pixels_zx0.data(), compressed.pixels_zx0.size(), 8000);
    auto dec_fg     = zx0_decompress(compressed.fg_zx0.data(), compressed.fg_zx0.size(), 4000);
    auto dec_bg     = zx0_decompress(compressed.bg_zx0.data(), compressed.bg_zx0.size(), 4000);

    // Check decompressed matches pre-compression streams
    if (dec_pixels != streams.pixels)
        throw std::runtime_error("verify: pixel stream mismatch after decompression");
    if (dec_fg != streams.fg_packed)
        throw std::runtime_error("verify: fg stream mismatch after decompression");
    if (dec_bg != streams.bg_packed)
        throw std::runtime_error("verify: bg stream mismatch after decompression");

    // Unpack nibbles
    auto fg_nibbles = unpack_nibble_pairs(dec_fg);
    auto bg_nibbles = unpack_nibble_pairs(dec_bg);

    // Reorder to row-major
    auto row_pixels = reorder_col_to_row(dec_pixels);
    auto row_fg     = reorder_col_to_row(fg_nibbles);
    auto row_bg     = reorder_col_to_row(bg_nibbles);

    // Visual equivalence check
    for (int i = 0; i < TOTAL_BLOCKS; i++) {
        uint8_t orig_fg = (orig_colors[i] >> 4) & 0x0F;
        uint8_t orig_bg = orig_colors[i] & 0x0F;
        if (!blocks_visually_equal(orig_pixels[i], orig_fg, orig_bg,
                                   row_pixels[i], row_fg[i], row_bg[i])) {
            throw std::runtime_error("verify: visual mismatch at block " + std::to_string(i));
        }
    }
}

// ─── File I/O ────────────────────────────────────────────────────────────────

std::vector<uint8_t> read_bin(const std::string& path, size_t expected_size) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) throw std::runtime_error("Cannot open file: " + path);

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (static_cast<size_t>(size) != expected_size) {
        fclose(f);
        throw std::runtime_error("File " + path + " has size " + std::to_string(size) +
                                 ", expected " + std::to_string(expected_size));
    }

    std::vector<uint8_t> data(expected_size);
    if (fread(data.data(), 1, expected_size, f) != expected_size) {
        fclose(f);
        throw std::runtime_error("Failed to read file: " + path);
    }
    fclose(f);
    return data;
}

void write_output(const CompressedStreams& c, const std::string& path) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) throw std::runtime_error("Cannot open output: " + path);
    auto write_all = [&](const std::vector<uint8_t>& v) {
        if (fwrite(v.data(), 1, v.size(), f) != v.size()) {
            fclose(f);
            throw std::runtime_error("Write failed: " + path);
        }
    };
    write_all(c.pixels_zx0);
    write_all(c.fg_zx0);
    write_all(c.bg_zx0);
    fclose(f);
}

// ─── CLI ─────────────────────────────────────────────────────────────────────

Options parse_args(int argc, char* argv[]) {
    Options opts;
    std::vector<std::string> positional;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-o" && i + 1 < argc) {
            opts.output_stem = argv[++i];
        } else if (arg[0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", arg.c_str());
            fprintf(stderr, "Usage: mo5z <pixels.bin> <colors.bin> [-o <stem>]\n");
            exit(1);
        } else {
            positional.push_back(arg);
        }
    }

    if (positional.size() != 2) {
        fprintf(stderr, "Usage: mo5z <pixels.bin> <colors.bin> [-o <stem>]\n");
        exit(1);
    }

    opts.pixels_path = positional[0];
    opts.colors_path = positional[1];
    return opts;
}

// ─── Stats ───────────────────────────────────────────────────────────────────

void print_stats(const CompressedStreams& c, const std::string& output_path) {
    auto pct = [](size_t compressed, size_t original) {
        return 100.0 * compressed / original;
    };
    size_t total_in = 8000 + 4000 + 4000;
    size_t total_out = c.pixels_zx0.size() + c.fg_zx0.size() + c.bg_zx0.size();
    printf("pixels: %5zu → %5zu (%.1f%%)\n", (size_t)8000, c.pixels_zx0.size(), pct(c.pixels_zx0.size(), 8000));
    printf("fg:     %5zu → %5zu (%.1f%%)\n", (size_t)4000, c.fg_zx0.size(), pct(c.fg_zx0.size(), 4000));
    printf("bg:     %5zu → %5zu (%.1f%%)\n", (size_t)4000, c.bg_zx0.size(), pct(c.bg_zx0.size(), 4000));
    printf("total: %5zu → %5zu (%.1f%%)\n", total_in, total_out, pct(total_out, total_in));
    printf("wrote %s\n", output_path.c_str());
}

// ─── Main ────────────────────────────────────────────────────────────────────

#ifndef MO5Z_TESTING
int main(int argc, char* argv[]) {
    try {
        Options opts = parse_args(argc, argv);
        auto pixels = read_bin(opts.pixels_path, 8000);
        auto colors = read_bin(opts.colors_path, 8000);

        Streams streams = build_streams(pixels.data(), colors.data());

        CompressedStreams compressed;
        compressed.pixels_zx0 = zx0_compress(streams.pixels);
        compressed.fg_zx0     = zx0_compress(streams.fg_packed);
        compressed.bg_zx0     = zx0_compress(streams.bg_packed);

        verify(streams, compressed, pixels.data(), colors.data());

        std::string output_path = opts.output_stem + ".mo5z";
        write_output(compressed, output_path);
        print_stats(compressed, output_path);
        return 0;
    } catch (const std::exception& e) {
        fprintf(stderr, "Error: %s\n", e.what());
        return 1;
    }
}
#endif
