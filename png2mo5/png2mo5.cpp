// png2mo5 — Convert any image to Thomson MO5 video format
// Single-file C++17 implementation. See DESIGN.md for specification.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <array>
#include <filesystem>
#include <string>
#include <vector>
#include <stdexcept>

namespace fs = std::filesystem;

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image.h"
#include "stb/stb_image_resize2.h"
#include "stb/stb_image_write.h"

// ============================================================================
// Constants
// ============================================================================

constexpr int MO5_W          = 320;
constexpr int MO5_H          = 200;
constexpr int BLOCK_W        = 8;
constexpr int BLOCKS_PER_ROW = MO5_W / BLOCK_W;  // 40
constexpr int TOTAL_BLOCKS   = BLOCKS_PER_ROW * MO5_H;  // 8000
constexpr int NUM_COLORS     = 16;
constexpr int NUM_PAIRS      = (NUM_COLORS * (NUM_COLORS + 1)) / 2;  // 136
constexpr float ERROR_DAMPING = 0.9f;

// ============================================================================
// Data Structures
// ============================================================================

struct Lab {
    float L;  // [0, 100]
    float a;  // [-128, 127]
    float b;  // [-128, 127]
};

struct Palette {
    uint8_t rgb[NUM_COLORS][3];
    Lab     lab[NUM_COLORS];
};

struct BlockResult {
    uint8_t fg;
    uint8_t bg;
    uint8_t pixels_byte;
};

struct Mo5Screen {
    uint8_t pixels[TOTAL_BLOCKS];
    uint8_t colors[TOTAL_BLOCKS];
};

struct Options {
    std::string input_path;
    std::string output_basename;
    bool nearest          = false;
    bool no_dither        = false;
    bool bin              = false;
    bool preview          = false;
    bool explicit_output  = false;
    float error_damping   = ERROR_DAMPING;
};

struct ColorPair {
    uint8_t c1;
    uint8_t c2;
};

// ============================================================================
// Compile-time pair table
// ============================================================================

// Build the 136 unique color pairs (C(16,2) + 16 same-color) at compile time.
constexpr std::array<ColorPair, NUM_PAIRS> build_all_pairs() {
    std::array<ColorPair, NUM_PAIRS> pairs{};
    int idx = 0;
    for (uint8_t c1 = 0; c1 < NUM_COLORS; ++c1)
        for (uint8_t c2 = c1; c2 < NUM_COLORS; ++c2)
            pairs[idx++] = {c1, c2};
    return pairs;
}

constexpr std::array<ColorPair, NUM_PAIRS> ALL_PAIRS = build_all_pairs();

// ============================================================================
// Color Space Conversion
// ============================================================================

// Decode sRGB gamma curve to linear light. The piecewise function avoids
// numerical issues near zero.
float srgb_to_linear(float s) {
    if (s <= 0.04045f)
        return s / 12.92f;
    return std::pow((s + 0.055f) / 1.055f, 2.4f);
}

// Encode linear light back to sRGB gamma curve.
float linear_to_srgb(float linear) {
    if (linear <= 0.0031308f)
        return 12.92f * linear;
    return 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
}

struct XYZ { float X, Y, Z; };

// Convert linear RGB to CIE XYZ (D65 illuminant) via the sRGB matrix.
XYZ linear_to_xyz(float r, float g, float b) {
    return {
        0.4124564f * r + 0.3575761f * g + 0.1804375f * b,
        0.2126729f * r + 0.7151522f * g + 0.0721750f * b,
        0.0193339f * r + 0.1191920f * g + 0.9503041f * b
    };
}

// Convert CIE XYZ to CIELAB. Uses the standard piecewise f(t) that linearizes
// near black to avoid infinite slope at zero.
Lab xyz_to_lab(float X, float Y, float Z) {
    constexpr float Xn = 0.95047f;
    constexpr float Yn = 1.0f;
    constexpr float Zn = 1.08883f;
    constexpr float epsilon = 0.008856f;  // (6/29)^3
    constexpr float kappa_inv = 1.0f / (3.0f * 0.04280f);  // 1/(3*(6/29)^2)
    constexpr float offset = 4.0f / 29.0f;

    auto f = [&](float t) -> float {
        if (t > epsilon)
            return std::cbrt(t);
        return t * kappa_inv + offset;
    };

    float fx = f(X / Xn);
    float fy = f(Y / Yn);
    float fz = f(Z / Zn);

    return {
        116.0f * fy - 16.0f,
        500.0f * (fx - fy),
        200.0f * (fy - fz)
    };
}

// Full pipeline: 8-bit sRGB → linear → XYZ → Lab.
Lab srgb_to_lab(uint8_t r, uint8_t g, uint8_t b) {
    float lr = srgb_to_linear(r / 255.0f);
    float lg = srgb_to_linear(g / 255.0f);
    float lb = srgb_to_linear(b / 255.0f);
    XYZ xyz = linear_to_xyz(lr, lg, lb);
    return xyz_to_lab(xyz.X, xyz.Y, xyz.Z);
}

// Reverse pipeline: Lab → XYZ → linear RGB → sRGB 8-bit. Used for preview rendering.
void lab_to_srgb_out(Lab c, uint8_t& r, uint8_t& g, uint8_t& b) {
    // Lab → XYZ
    constexpr float Xn = 0.95047f;
    constexpr float Yn = 1.0f;
    constexpr float Zn = 1.08883f;

    float fy = (c.L + 16.0f) / 116.0f;
    float fx = c.a / 500.0f + fy;
    float fz = fy - c.b / 200.0f;

    constexpr float delta = 6.0f / 29.0f;
    auto inv_f = [&](float t) -> float {
        if (t > delta)
            return t * t * t;
        return 3.0f * delta * delta * (t - 4.0f / 29.0f);
    };

    float X = Xn * inv_f(fx);
    float Y = Yn * inv_f(fy);
    float Z = Zn * inv_f(fz);

    // XYZ → linear RGB (inverse of sRGB matrix)
    float lr =  3.2404542f * X - 1.5371385f * Y - 0.4985314f * Z;
    float lg = -0.9692660f * X + 1.8760108f * Y + 0.0415560f * Z;
    float lb =  0.0556434f * X - 0.2040259f * Y + 1.0572252f * Z;

    // linear → sRGB → clamp → uint8
    auto to_u8 = [](float v) -> uint8_t {
        v = std::clamp(linear_to_srgb(std::clamp(v, 0.0f, 1.0f)), 0.0f, 1.0f);
        return static_cast<uint8_t>(std::round(v * 255.0f));
    };

    r = to_u8(lr);
    g = to_u8(lg);
    b = to_u8(lb);
}

// Squared CIE76 distance in Lab space. No sqrt needed since we only compare.
float lab_distance_sq(Lab a, Lab b) {
    float dL = a.L - b.L;
    float da = a.a - b.a;
    float db = a.b - b.b;
    return dL * dL + da * da + db * db;
}

// ============================================================================
// Palette
// ============================================================================

// Initialize the fixed MO5 16-color palette with both sRGB and pre-computed Lab values.
Palette init_palette() {
    Palette pal{};
    // MO5 fixed 16-color palette (sRGB)
    static constexpr uint8_t rgb_table[16][3] = {
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
    for (int i = 0; i < 16; ++i) {
        pal.rgb[i][0] = rgb_table[i][0];
        pal.rgb[i][1] = rgb_table[i][1];
        pal.rgb[i][2] = rgb_table[i][2];
        pal.lab[i] = srgb_to_lab(rgb_table[i][0], rgb_table[i][1], rgb_table[i][2]);
    }
    return pal;
}

// Find the palette color with minimum Lab distance to a given pixel.
uint8_t nearest_color(Lab pixel, const Palette& pal) {
    uint8_t best = 0;
    float best_dist = lab_distance_sq(pixel, pal.lab[0]);
    for (int i = 1; i < NUM_COLORS; ++i) {
        float d = lab_distance_sq(pixel, pal.lab[i]);
        if (d < best_dist) {
            best_dist = d;
            best = static_cast<uint8_t>(i);
        }
    }
    return best;
}

// ============================================================================
// Bit Manipulation
// ============================================================================

// Reverse the bit order of a byte. Needed to flip the bitmap on RTL scanlines
// so pixel data matches the MO5's MSB-left layout regardless of scan direction.
uint8_t reverse_bits(uint8_t b) {
    b = ((b & 0xF0) >> 4) | ((b & 0x0F) << 4);
    b = ((b & 0xCC) >> 2) | ((b & 0x33) << 2);
    b = ((b & 0xAA) >> 1) | ((b & 0x55) << 1);
    return b;
}

// ============================================================================
// Best Pair Selection
// ============================================================================

// Choose the best 2-color pair for an 8-pixel block. Brute-forces all 136
// palette pairs, simulating Floyd-Steinberg within the block for each candidate.
// Picks the pair that minimizes total outgoing error (error that will leak into
// neighboring blocks), which directly optimizes global image quality.
BlockResult select_pair(const Lab* block_pixels, const Palette& pal) {
    float best_outgoing = 1e30f;
    uint8_t best_c1 = 0, best_c2 = 0;
    uint8_t best_bitmap = 0;

    for (const auto& pair : ALL_PAIRS) {
        // Trial FS on a scratch copy
        Lab scratch[BLOCK_W];
        for (int i = 0; i < BLOCK_W; ++i)
            scratch[i] = block_pixels[i];

        float outgoing_error = 0.0f;
        uint8_t bitmap = 0;

        for (int i = 0; i < BLOCK_W; ++i) {
            // Pick nearest of c1, c2
            float d1 = lab_distance_sq(scratch[i], pal.lab[pair.c1]);
            float d2 = lab_distance_sq(scratch[i], pal.lab[pair.c2]);
            uint8_t chosen = (d1 <= d2) ? pair.c1 : pair.c2;
            if (d1 <= d2)
                bitmap |= (1 << (7 - i));

            // Compute error vector
            Lab error;
            error.L = scratch[i].L - pal.lab[chosen].L;
            error.a = scratch[i].a - pal.lab[chosen].a;
            error.b = scratch[i].b - pal.lab[chosen].b;

            float magnitude = error.L * error.L + error.a * error.a + error.b * error.b;

            // Intra-block rightward diffusion (7/16)
            if (i < BLOCK_W - 1) {
                scratch[i + 1].L += error.L * (7.0f / 16.0f);
                scratch[i + 1].a += error.a * (7.0f / 16.0f);
                scratch[i + 1].b += error.b * (7.0f / 16.0f);
                // Outgoing = downward directions only (9/16)
                outgoing_error += magnitude * (9.0f / 16.0f);
            } else {
                // Last pixel: rightward has nowhere to go → all is outgoing
                outgoing_error += magnitude;
            }
        }

        if (outgoing_error < best_outgoing) {
            best_outgoing = outgoing_error;
            best_c1 = pair.c1;
            best_c2 = pair.c2;
            best_bitmap = bitmap;
        }
    }

    return {best_c1, best_c2, best_bitmap};
}

// ============================================================================
// Error Diffusion
// ============================================================================

// Spread quantization error to neighboring pixels using Floyd-Steinberg weights.
// The kernel is mirrored on RTL rows so diffusion always flows in scan direction.
void diffuse_error(Lab* work, int px, int py, Lab error, bool ltr) {
    int fwd = ltr ? 1 : -1;

    auto add = [&](int x, int y, float w) {
        if (x < 0 || x >= MO5_W || y < 0 || y >= MO5_H) return;
        int idx = y * MO5_W + x;
        work[idx].L = std::clamp(work[idx].L + error.L * w, 0.0f, 100.0f);
        work[idx].a = std::clamp(work[idx].a + error.a * w, -128.0f, 127.0f);
        work[idx].b = std::clamp(work[idx].b + error.b * w, -128.0f, 127.0f);
    };

    add(px + fwd, py,     7.0f / 16.0f);
    add(px - fwd, py + 1, 3.0f / 16.0f);
    add(px,       py + 1, 5.0f / 16.0f);
    add(px + fwd, py + 1, 1.0f / 16.0f);
}

// ============================================================================
// Image Loading & Scaling
// ============================================================================

// Load any image via stb, scale to fill 320×200 (preserving aspect ratio),
// and center-crop the overflow. Uses Lanczos-like filter by default, or
// nearest-neighbor for pixel art.
std::vector<uint8_t> load_and_scale(const char* path, bool nearest) {
    int w, h, channels;
    uint8_t* data = stbi_load(path, &w, &h, &channels, 3);
    if (!data) {
        std::fprintf(stderr, "Error: cannot load '%s': %s\n", path, stbi_failure_reason());
        std::exit(1);
    }

    // Scale to fill 320×200 completely, cropping overflow
    float scale = std::max(320.0f / w, 200.0f / h);
    int sw = std::max(1, static_cast<int>(std::round(w * scale)));
    int sh = std::max(1, static_cast<int>(std::round(h * scale)));

    std::vector<uint8_t> tmp(sw * sh * 3);

    if (nearest) {
        STBIR_RESIZE resize;
        stbir_resize_init(&resize, data, w, h, 0, tmp.data(), sw, sh, 0,
                          STBIR_RGB, STBIR_TYPE_UINT8);
        stbir_set_filters(&resize, STBIR_FILTER_POINT_SAMPLE, STBIR_FILTER_POINT_SAMPLE);
        stbir_resize_extended(&resize);
    } else {
        stbir_resize_uint8_srgb(data, w, h, 0, tmp.data(), sw, sh, 0, STBIR_RGB);
    }

    // Crop center to 320×200
    std::vector<uint8_t> output(MO5_W * MO5_H * 3, 0);
    int cx = (sw - MO5_W) / 2;
    int cy = (sh - MO5_H) / 2;
    for (int r = 0; r < MO5_H; ++r) {
        std::memcpy(output.data() + r * MO5_W * 3,
                    tmp.data() + ((cy + r) * sw + cx) * 3,
                    MO5_W * 3);
    }

    stbi_image_free(data);
    return output;
}

// ============================================================================
// Conversion Pipeline
// ============================================================================

// Batch-convert the entire 320×200 RGB buffer to Lab working space.
void convert_to_lab(const uint8_t* rgb, Lab* out) {
    for (int i = 0; i < MO5_W * MO5_H; ++i) {
        out[i] = srgb_to_lab(rgb[i * 3], rgb[i * 3 + 1], rgb[i * 3 + 2]);
    }
}

// Core conversion: processes the image block-by-block in serpentine order,
// selecting the best color pair per block then diffusing quantization error
// to downstream pixels via damped Floyd-Steinberg.
void convert(const uint8_t* rgb_320x200, const Options& opts, Mo5Screen& screen) {
    std::vector<Lab> work_buf(MO5_W * MO5_H);
    convert_to_lab(rgb_320x200, work_buf.data());

    Palette pal = init_palette();

    for (int y = 0; y < MO5_H; ++y) {
        bool ltr = (y % 2 == 0);
        int block_start = ltr ? 0 : BLOCKS_PER_ROW - 1;
        int block_end   = ltr ? BLOCKS_PER_ROW : -1;
        int block_step  = ltr ? 1 : -1;

        for (int bx = block_start; bx != block_end; bx += block_step) {
            int px0 = bx * BLOCK_W;

            // Read 8 pixels in scan direction
            Lab block[BLOCK_W];
            if (ltr) {
                for (int i = 0; i < BLOCK_W; ++i)
                    block[i] = work_buf[y * MO5_W + px0 + i];
            } else {
                for (int i = 0; i < BLOCK_W; ++i)
                    block[i] = work_buf[y * MO5_W + px0 + (7 - i)];
            }

            // Find best color pair
            BlockResult br = select_pair(block, pal);

            // RTL: reverse bitmap bits to match screen layout
            if (!ltr) br.pixels_byte = reverse_bits(br.pixels_byte);

            // Store into Mo5Screen
            int offset = y * BLOCKS_PER_ROW + bx;
            screen.pixels[offset] = br.pixels_byte;
            screen.colors[offset] = (br.fg << 4) | br.bg;

            // Per-pixel error diffusion
            if (!opts.no_dither) {
                for (int i = 0; i < BLOCK_W; ++i) {
                    int px;
                    if (ltr) px = px0 + i;
                    else     px = px0 + (7 - i);

                    Lab current = work_buf[y * MO5_W + px];

                    int bit = (br.pixels_byte >> (7 - (px - px0))) & 1;
                    uint8_t chosen = bit ? br.fg : br.bg;
                    Lab chosen_lab = pal.lab[chosen];

                    Lab error;
                    error.L = (current.L - chosen_lab.L) * opts.error_damping;
                    error.a = (current.a - chosen_lab.a) * opts.error_damping;
                    error.b = (current.b - chosen_lab.b) * opts.error_damping;

                    diffuse_error(work_buf.data(), px, y, error, ltr);
                }
            }
        }
    }
}

// ============================================================================
// Output
// ============================================================================

// Write raw bytes to a file. Fatal on failure.
void write_bin(const char* path, const uint8_t* data, size_t len) {
    FILE* f = std::fopen(path, "wb");
    if (!f) {
        std::fprintf(stderr, "Error: cannot write '%s'\n", path);
        std::exit(1);
    }
    if (std::fwrite(data, 1, len, f) != len) {
        std::fclose(f);
        std::fprintf(stderr, "Error: write failed for '%s'\n", path);
        std::exit(1);
    }
    std::fclose(f);
}

// Render the Mo5Screen back to a 320×200 RGB image using the palette,
// then write as PNG. Shows exactly what the MO5 would display.
void write_preview(const char* path, const Mo5Screen& screen, const Palette& pal) {
    std::vector<uint8_t> buf(MO5_W * MO5_H * 3);
    for (int i = 0; i < TOTAL_BLOCKS; ++i) {
        uint8_t fg = screen.colors[i] >> 4;
        uint8_t bg = screen.colors[i] & 0x0F;
        int y  = i / BLOCKS_PER_ROW;
        int x0 = (i % BLOCKS_PER_ROW) * BLOCK_W;
        for (int bit = 7; bit >= 0; --bit) {
            uint8_t color_idx = ((screen.pixels[i] >> bit) & 1) ? fg : bg;
            int px = x0 + (7 - bit);
            int off = (y * MO5_W + px) * 3;
            buf[off + 0] = pal.rgb[color_idx][0];
            buf[off + 1] = pal.rgb[color_idx][1];
            buf[off + 2] = pal.rgb[color_idx][2];
        }
    }
    if (!stbi_write_png(path, MO5_W, MO5_H, 3, buf.data(), MO5_W * 3)) {
        std::fprintf(stderr, "Error: cannot write preview '%s'\n", path);
        std::exit(1);
    }
}

// ============================================================================
// CLI Argument Parsing
// ============================================================================

// Parse CLI arguments per DESIGN.md interface spec.
Options parse_args(int argc, char** argv) {
    Options opts;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--nearest") {
            opts.nearest = true;
        } else if (arg == "--no-dither") {
            opts.no_dither = true;
        } else if (arg == "--bin") {
            opts.bin = true;
        } else if (arg == "--preview") {
            opts.preview = true;
        } else if (arg == "-o") {
            if (++i >= argc)
                throw std::runtime_error("-o requires an output path");
            opts.output_basename = argv[i];
            opts.explicit_output = true;
        } else if (arg == "--damping") {
            if (++i >= argc)
                throw std::runtime_error("--damping requires a value (0.0–1.0)");
            float val = std::strtof(argv[i], nullptr);
            if (val < 0.0f || val > 1.0f)
                throw std::runtime_error("--damping must be between 0.0 and 1.0");
            opts.error_damping = val;
        } else if (arg.size() > 1 && arg[0] == '-') {
            throw std::runtime_error(
                "Unknown flag: " + arg + "\n"
                "Usage: png2mo5 input.png [-o output] [--bin] [--preview] [--nearest] [--no-dither] [--damping F]");
        } else {
            if (opts.input_path.empty())
                opts.input_path = arg;
            else
                throw std::runtime_error(
                    "Unexpected argument: " + arg + "\n"
                    "Usage: png2mo5 input.png [-o output] [--bin] [--preview] [--nearest] [--no-dither] [--damping F]");
        }
    }

    if (opts.input_path.empty()) {
        throw std::runtime_error(
            "Usage: png2mo5 input.png [-o output] [--bin] [--preview] [--nearest] [--no-dither] [--damping F]");
    }

    if (opts.output_basename.empty()) {
        opts.output_basename = fs::path(opts.input_path).stem().string();
    }

    return opts;
}

// ============================================================================
// Main
// ============================================================================

#ifndef PNG2MO5_TESTING
int main(int argc, char** argv) {
    Options opts;
    try {
        opts = parse_args(argc, argv);
    } catch (const std::runtime_error& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }

    auto rgb = load_and_scale(opts.input_path.c_str(), opts.nearest);

    Mo5Screen screen{};
    convert(rgb.data(), opts, screen);

    if (opts.bin) {
        std::string pixels_path = opts.output_basename + "_pixels.bin";
        std::string colors_path = opts.output_basename + "_colors.bin";
        write_bin(pixels_path.c_str(), screen.pixels, 8000);
        write_bin(colors_path.c_str(), screen.colors, 8000);
        std::printf("Wrote %s (%d bytes)\n", pixels_path.c_str(), 8000);
        std::printf("Wrote %s (%d bytes)\n", colors_path.c_str(), 8000);
    }

    if (opts.preview || !opts.bin) {
        Palette pal = init_palette();
        std::string preview_path = opts.explicit_output
            ? opts.output_basename + ".png"
            : opts.output_basename + "_preview.png";
        write_preview(preview_path.c_str(), screen, pal);
        std::printf("Wrote %s\n", preview_path.c_str());
    }

    return 0;
}
#endif
