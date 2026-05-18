#define PNG2MO5_TESTING
#include "png2mo5.cpp"
#include "catch2/catch_amalgamated.hpp"

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

// ============================================================================
// 10.1  Color Space Conversion Tests
// ============================================================================

TEST_CASE("srgb_to_linear") {
    SECTION("zero") {
        REQUIRE(srgb_to_linear(0.0f) == 0.0f);
    }
    SECTION("one") {
        REQUIRE(srgb_to_linear(1.0f) == 1.0f);
    }
    SECTION("below breakpoint") {
        REQUIRE_THAT(srgb_to_linear(0.04f), WithinAbs(0.04f / 12.92f, 1e-6));
    }
    SECTION("at breakpoint") {
        REQUIRE_THAT(srgb_to_linear(0.04045f), WithinAbs(0.04045f / 12.92f, 1e-6));
    }
    SECTION("above breakpoint") {
        float expected = std::pow((0.5f + 0.055f) / 1.055f, 2.4f);
        REQUIRE_THAT(srgb_to_linear(0.5f), WithinAbs(expected, 1e-5));
    }
    SECTION("mid-range") {
        float expected = std::pow((0.2f + 0.055f) / 1.055f, 2.4f);
        REQUIRE_THAT(srgb_to_linear(0.2f), WithinAbs(expected, 1e-5));
    }
}

TEST_CASE("linear_to_xyz") {
    SECTION("black") {
        auto xyz = linear_to_xyz(0, 0, 0);
        REQUIRE(xyz.X == 0.0f);
        REQUIRE(xyz.Y == 0.0f);
        REQUIRE(xyz.Z == 0.0f);
    }
    SECTION("white") {
        auto xyz = linear_to_xyz(1, 1, 1);
        REQUIRE_THAT(xyz.X, WithinAbs(0.9505f, 0.001f));
        REQUIRE_THAT(xyz.Y, WithinAbs(1.0f, 0.001f));
        REQUIRE_THAT(xyz.Z, WithinAbs(1.089f, 0.001f));
    }
    SECTION("pure red") {
        auto xyz = linear_to_xyz(1, 0, 0);
        REQUIRE_THAT(xyz.X, WithinAbs(0.4125f, 0.001f));
        REQUIRE_THAT(xyz.Y, WithinAbs(0.2127f, 0.001f));
        REQUIRE_THAT(xyz.Z, WithinAbs(0.0193f, 0.001f));
    }
}

TEST_CASE("xyz_to_lab") {
    SECTION("D65 white") {
        Lab lab = xyz_to_lab(0.95047f, 1.0f, 1.08883f);
        REQUIRE_THAT(lab.L, WithinAbs(100.0f, 0.1f));
        REQUIRE_THAT(lab.a, WithinAbs(0.0f, 0.5f));
        REQUIRE_THAT(lab.b, WithinAbs(0.0f, 0.5f));
    }
    SECTION("black") {
        Lab lab = xyz_to_lab(0, 0, 0);
        REQUIRE_THAT(lab.L, WithinAbs(0.0f, 0.1f));
        REQUIRE_THAT(lab.a, WithinAbs(0.0f, 0.1f));
        REQUIRE_THAT(lab.b, WithinAbs(0.0f, 0.1f));
    }
}

TEST_CASE("srgb_to_lab round-trip through known values") {
    Palette pal = init_palette();
    for (int i = 0; i < 16; ++i) {
        REQUIRE(pal.colors[i].L >= 0.0f);
        REQUIRE(pal.colors[i].L <= 100.0f);
    }
    // Black → L ≈ 0
    REQUIRE_THAT(pal.colors[0].L, WithinAbs(0.0f, 0.1f));
    // White → L ≈ 100
    REQUIRE_THAT(pal.colors[7].L, WithinAbs(100.0f, 0.1f));
    // Red → positive a*
    REQUIRE(pal.colors[1].a > 30.0f);
}

TEST_CASE("lab_distance_sq") {
    SECTION("same point") {
        Lab a{50, 20, -10};
        REQUIRE(lab_distance_sq(a, a) == 0.0f);
    }
    SECTION("known offset") {
        Lab a{50, 0, 0};
        Lab b{60, 3, 4};
        REQUIRE_THAT(lab_distance_sq(a, b), WithinAbs(125.0f, 0.001f));
    }
    SECTION("symmetry") {
        Lab a{50, 20, -10};
        Lab b{60, 3, 4};
        REQUIRE(lab_distance_sq(a, b) == lab_distance_sq(b, a));
    }
}

// ============================================================================
// 10.2  Palette Tests
// ============================================================================

TEST_CASE("init_palette RGB values") {
    Palette pal = init_palette();
    REQUIRE(pal.rgb[0][0] == 0);   REQUIRE(pal.rgb[0][1] == 0);   REQUIRE(pal.rgb[0][2] == 0);
    REQUIRE(pal.rgb[1][0] == 255); REQUIRE(pal.rgb[1][1] == 0);   REQUIRE(pal.rgb[1][2] == 0);
    REQUIRE(pal.rgb[2][0] == 0);   REQUIRE(pal.rgb[2][1] == 255); REQUIRE(pal.rgb[2][2] == 0);
    REQUIRE(pal.rgb[3][0] == 255); REQUIRE(pal.rgb[3][1] == 255); REQUIRE(pal.rgb[3][2] == 0);
    REQUIRE(pal.rgb[4][0] == 0);   REQUIRE(pal.rgb[4][1] == 0);   REQUIRE(pal.rgb[4][2] == 255);
    REQUIRE(pal.rgb[5][0] == 255); REQUIRE(pal.rgb[5][1] == 0);   REQUIRE(pal.rgb[5][2] == 255);
    REQUIRE(pal.rgb[6][0] == 0);   REQUIRE(pal.rgb[6][1] == 255); REQUIRE(pal.rgb[6][2] == 255);
    REQUIRE(pal.rgb[7][0] == 255); REQUIRE(pal.rgb[7][1] == 255); REQUIRE(pal.rgb[7][2] == 255);
    REQUIRE(pal.rgb[8][0] == 128); REQUIRE(pal.rgb[8][1] == 128); REQUIRE(pal.rgb[8][2] == 128);
    REQUIRE(pal.rgb[9][0] == 255); REQUIRE(pal.rgb[9][1] == 128); REQUIRE(pal.rgb[9][2] == 128);
    REQUIRE(pal.rgb[10][0] == 128); REQUIRE(pal.rgb[10][1] == 255); REQUIRE(pal.rgb[10][2] == 128);
    REQUIRE(pal.rgb[11][0] == 255); REQUIRE(pal.rgb[11][1] == 255); REQUIRE(pal.rgb[11][2] == 128);
    REQUIRE(pal.rgb[12][0] == 128); REQUIRE(pal.rgb[12][1] == 128); REQUIRE(pal.rgb[12][2] == 255);
    REQUIRE(pal.rgb[13][0] == 255); REQUIRE(pal.rgb[13][1] == 128); REQUIRE(pal.rgb[13][2] == 255);
    REQUIRE(pal.rgb[14][0] == 128); REQUIRE(pal.rgb[14][1] == 255); REQUIRE(pal.rgb[14][2] == 255);
    REQUIRE(pal.rgb[15][0] == 255); REQUIRE(pal.rgb[15][1] == 128); REQUIRE(pal.rgb[15][2] == 0);
}

TEST_CASE("init_palette Lab values are reasonable") {
    Palette pal = init_palette();
    REQUIRE_THAT(pal.colors[0].L, WithinAbs(0.0f, 0.1f));  // black
    REQUIRE_THAT(pal.colors[7].L, WithinAbs(100.0f, 0.1f)); // white
    REQUIRE(pal.colors[1].a > 0.0f);  // red: positive a*
    REQUIRE(pal.colors[2].a < 0.0f);  // green: negative a*
    for (int i = 0; i < 16; ++i) {
        REQUIRE(pal.colors[i].L >= 0.0f);
        REQUIRE(pal.colors[i].L <= 100.0f);
    }
}

TEST_CASE("nearest_color") {
    Palette pal = init_palette();
    REQUIRE(nearest_color(srgb_to_lab(0, 0, 0), pal) == 0);       // exact black
    REQUIRE(nearest_color(srgb_to_lab(255, 255, 255), pal) == 7);  // exact white
    REQUIRE(nearest_color(srgb_to_lab(200, 10, 10), pal) == 1);    // close to red
    REQUIRE(nearest_color(srgb_to_lab(130, 130, 130), pal) == 8);  // close to gray
    REQUIRE(nearest_color(srgb_to_lab(255, 100, 0), pal) == 15);   // close to orange
    REQUIRE(nearest_color(srgb_to_lab(0, 200, 0), pal) == 2);      // close to green
}

// ============================================================================
// 10.3  Bit Manipulation
// ============================================================================

TEST_CASE("reverse_bits") {
    REQUIRE(reverse_bits(0x00) == 0x00);
    REQUIRE(reverse_bits(0xFF) == 0xFF);
    REQUIRE(reverse_bits(0x80) == 0x01);
    REQUIRE(reverse_bits(0x01) == 0x80);
    REQUIRE(reverse_bits(0xB4) == 0x2D);

    // Involution: reverse(reverse(x)) == x for all bytes
    for (int x = 0; x < 256; ++x) {
        REQUIRE(reverse_bits(reverse_bits(static_cast<uint8_t>(x))) == static_cast<uint8_t>(x));
    }
}

// ============================================================================
// 10.4  Best Pair Selection
// ============================================================================

TEST_CASE("select_pair — uniform block") {
    Palette pal = init_palette();
    Lab block[8];
    for (int i = 0; i < 8; ++i) block[i] = pal.colors[3];  // all yellow

    BlockResult br = select_pair(block, pal);
    // Both fg and bg should be 3 (yellow), OR one of them is 3 with matching bitmap
    bool ok = (br.fg == 3 && br.bg == 3) ||
              (br.fg == 3 && br.pixels_byte == 0xFF) ||
              (br.bg == 3 && br.pixels_byte == 0x00);
    REQUIRE(ok);
}

TEST_CASE("select_pair — two-color block") {
    Palette pal = init_palette();
    Lab block[8];
    // alternating red (1) and blue (4)
    for (int i = 0; i < 8; ++i)
        block[i] = pal.colors[(i % 2 == 0) ? 1 : 4];

    BlockResult br = select_pair(block, pal);
    // Pair should be {1,4} in some order
    bool has_both = (br.fg == 1 && br.bg == 4) || (br.fg == 4 && br.bg == 1);
    REQUIRE(has_both);
    // Bitmap should be 0xAA or 0x55
    REQUIRE((br.pixels_byte == 0xAA || br.pixels_byte == 0x55));
}

TEST_CASE("select_pair — all same color picks optimal pair") {
    Palette pal = init_palette();
    // Off-palette dark red
    Lab block[8];
    Lab dark_red = srgb_to_lab(120, 0, 0);
    for (int i = 0; i < 8; ++i) block[i] = dark_red;

    BlockResult br = select_pair(block, pal);

    // Property: chosen pair must have lowest outgoing error
    auto compute_outgoing = [&](uint8_t c1, uint8_t c2) -> float {
        Lab scratch[BLOCK_W];
        for (int i = 0; i < BLOCK_W; ++i) scratch[i] = block[i];
        float outgoing = 0.0f;
        for (int i = 0; i < BLOCK_W; ++i) {
            float d1 = lab_distance_sq(scratch[i], pal.colors[c1]);
            float d2 = lab_distance_sq(scratch[i], pal.colors[c2]);
            uint8_t chosen = (d1 <= d2) ? c1 : c2;
            Lab err;
            err.L = scratch[i].L - pal.colors[chosen].L;
            err.a = scratch[i].a - pal.colors[chosen].a;
            err.b = scratch[i].b - pal.colors[chosen].b;
            float mag = err.L * err.L + err.a * err.a + err.b * err.b;
            if (i < BLOCK_W - 1) {
                scratch[i + 1].L += err.L * (7.0f / 16.0f);
                scratch[i + 1].a += err.a * (7.0f / 16.0f);
                scratch[i + 1].b += err.b * (7.0f / 16.0f);
                outgoing += mag * (9.0f / 16.0f);
            } else {
                outgoing += mag;
            }
        }
        return outgoing;
    };

    float chosen_outgoing = compute_outgoing(br.fg, br.bg);
    for (const auto& pair : ALL_PAIRS) {
        float alt = compute_outgoing(pair.c1, pair.c2);
        REQUIRE(chosen_outgoing <= alt + 0.01f);
    }
}

TEST_CASE("select_pair — prefers pair with lower outgoing error") {
    Palette pal = init_palette();
    // Build a smooth ramp between black (0) and white (7).
    // The pair {0,7} brackets the ramp tightly — intra-block diffusion
    // absorbs error, yielding low outgoing error.
    // Compare against a pair like {0,8} (black+gray) which has a color
    // closer to mid-ramp pixels statically, but produces more outgoing
    // error because the trial-FS diffusion can't correct as well across
    // the full range.
    Lab block[8];
    for (int i = 0; i < 8; ++i) {
        // Ramp from L=0 to L=100
        float t = i / 7.0f;
        block[i].L = t * 100.0f;
        block[i].a = 0.0f;
        block[i].b = 0.0f;
    }

    BlockResult br = select_pair(block, pal);

    // Verify property: the chosen pair should have outgoing error <=
    // any other pair. Recompute outgoing error for the chosen pair
    // and verify it's minimal by spot-checking against a few alternatives.
    auto compute_outgoing = [&](uint8_t c1, uint8_t c2) -> float {
        Lab scratch[BLOCK_W];
        for (int i = 0; i < BLOCK_W; ++i) scratch[i] = block[i];
        float outgoing = 0.0f;
        for (int i = 0; i < BLOCK_W; ++i) {
            float d1 = lab_distance_sq(scratch[i], pal.colors[c1]);
            float d2 = lab_distance_sq(scratch[i], pal.colors[c2]);
            uint8_t chosen = (d1 <= d2) ? c1 : c2;
            Lab err;
            err.L = scratch[i].L - pal.colors[chosen].L;
            err.a = scratch[i].a - pal.colors[chosen].a;
            err.b = scratch[i].b - pal.colors[chosen].b;
            float mag = err.L * err.L + err.a * err.a + err.b * err.b;
            if (i < BLOCK_W - 1) {
                scratch[i + 1].L += err.L * (7.0f / 16.0f);
                scratch[i + 1].a += err.a * (7.0f / 16.0f);
                scratch[i + 1].b += err.b * (7.0f / 16.0f);
                outgoing += mag * (9.0f / 16.0f);
            } else {
                outgoing += mag;
            }
        }
        return outgoing;
    };

    float chosen_outgoing = compute_outgoing(br.fg, br.bg);
    // Check against all pairs — chosen must be minimal
    for (const auto& pair : ALL_PAIRS) {
        float alt = compute_outgoing(pair.c1, pair.c2);
        REQUIRE(chosen_outgoing <= alt + 0.01f);
    }
}

TEST_CASE("ERROR_DAMPING default") {
    REQUIRE(ERROR_DAMPING == Catch::Approx(0.9f));
    Options opts;
    REQUIRE(opts.error_damping == Catch::Approx(0.9f));
}

// ============================================================================
// 10.5  Error Diffusion Tests
// ============================================================================

TEST_CASE("diffuse_error — interior pixel LTR") {
    std::vector<Lab> work(MO5_W * MO5_H, {0, 0, 0});
    Lab error{16, 0, 0};
    diffuse_error(work.data(), 10, 5, error, true);

    REQUIRE_THAT(work[5 * MO5_W + 11].L, WithinAbs(7.0f, 0.01f));   // forward
    REQUIRE_THAT(work[6 * MO5_W + 9].L,  WithinAbs(3.0f, 0.01f));   // below-behind
    REQUIRE_THAT(work[6 * MO5_W + 10].L, WithinAbs(5.0f, 0.01f));   // below
    REQUIRE_THAT(work[6 * MO5_W + 11].L, WithinAbs(1.0f, 0.01f));   // below-forward
}

TEST_CASE("diffuse_error — interior pixel RTL") {
    std::vector<Lab> work(MO5_W * MO5_H, {0, 0, 0});
    Lab error{16, 0, 0};
    diffuse_error(work.data(), 10, 5, error, false);

    REQUIRE_THAT(work[5 * MO5_W + 9].L,  WithinAbs(7.0f, 0.01f));   // forward (left)
    REQUIRE_THAT(work[6 * MO5_W + 11].L, WithinAbs(3.0f, 0.01f));   // below-behind
    REQUIRE_THAT(work[6 * MO5_W + 10].L, WithinAbs(5.0f, 0.01f));   // below
    REQUIRE_THAT(work[6 * MO5_W + 9].L,  WithinAbs(1.0f, 0.01f));   // below-forward
}

TEST_CASE("diffuse_error — clamps to valid Lab ranges") {
    std::vector<Lab> work(MO5_W * MO5_H, {0, 0, 0});
    work[5 * MO5_W + 11] = {99, 0, 0};
    Lab error{32, 0, 0};
    diffuse_error(work.data(), 10, 5, error, true);
    // 99 + 32*7/16 = 99 + 14 = 113 → clamped to 100
    REQUIRE_THAT(work[5 * MO5_W + 11].L, WithinAbs(100.0f, 0.01f));

    // a clamping
    std::fill(work.begin(), work.end(), Lab{0, 0, 0});
    work[5 * MO5_W + 11] = {0, 120, 0};
    Lab error_a{0, 64, 0};
    diffuse_error(work.data(), 10, 5, error_a, true);
    REQUIRE_THAT(work[5 * MO5_W + 11].a, WithinAbs(127.0f, 0.01f));

    // b negative clamping
    std::fill(work.begin(), work.end(), Lab{0, 0, 0});
    work[5 * MO5_W + 11] = {0, 0, -120};
    Lab error_b{0, 0, -64};
    diffuse_error(work.data(), 10, 5, error_b, true);
    REQUIRE_THAT(work[5 * MO5_W + 11].b, WithinAbs(-128.0f, 0.01f));
}

TEST_CASE("diffuse_error — edge/corner pixels") {
    std::vector<Lab> work(MO5_W * MO5_H, {0, 0, 0});
    Lab error{16, 0, 0};

    // Top-left corner LTR: (-1,1) out of bounds, no crash
    diffuse_error(work.data(), 0, 0, error, true);
    REQUIRE_THAT(work[0 * MO5_W + 1].L, WithinAbs(7.0f, 0.01f));
    REQUIRE_THAT(work[1 * MO5_W + 0].L, WithinAbs(5.0f, 0.01f));
    REQUIRE_THAT(work[1 * MO5_W + 1].L, WithinAbs(1.0f, 0.01f));

    // Bottom-right corner: no valid neighbors below, no forward
    std::fill(work.begin(), work.end(), Lab{0, 0, 0});
    diffuse_error(work.data(), 319, 199, error, true);
    // All neighbors out of bounds → buffer unchanged (except potential below-behind)
    // (319+1 = 320 out, 319-1=318 and 200 out)
    // No crash is the key test
}

// ============================================================================
// 10.6  Full Conversion Integration Tests
// ============================================================================

TEST_CASE("convert — solid black image") {
    std::vector<uint8_t> rgb(MO5_W * MO5_H * 3, 0);
    Options opts;
    opts.no_dither = false;
    Mo5Screen screen{};
    convert(rgb.data(), opts, screen);

    for (int i = 0; i < TOTAL_BLOCKS; ++i) {
        // Both fg and bg should be 0 (black)
        REQUIRE(screen.colors[i] == 0x00);
    }
}

TEST_CASE("convert — solid white image") {
    std::vector<uint8_t> rgb(MO5_W * MO5_H * 3, 255);
    Options opts;
    opts.no_dither = false;
    Mo5Screen screen{};
    convert(rgb.data(), opts, screen);

    // Every pixel should render to white (palette index 7)
    for (int i = 0; i < TOTAL_BLOCKS; ++i) {
        uint8_t fg = screen.colors[i] >> 4;
        uint8_t bg = screen.colors[i] & 0x0F;
        for (int bit = 7; bit >= 0; --bit) {
            uint8_t c = ((screen.pixels[i] >> bit) & 1) ? fg : bg;
            REQUIRE(c == 7);
        }
    }
}

TEST_CASE("convert — vertical red/blue stripes by block") {
    std::vector<uint8_t> rgb(MO5_W * MO5_H * 3);
    for (int y = 0; y < MO5_H; ++y) {
        for (int x = 0; x < MO5_W; ++x) {
            int block = x / BLOCK_W;
            int off = (y * MO5_W + x) * 3;
            if (block % 2 == 0) {
                rgb[off] = 255; rgb[off + 1] = 0; rgb[off + 2] = 0;  // red
            } else {
                rgb[off] = 0; rgb[off + 1] = 0; rgb[off + 2] = 255;  // blue
            }
        }
    }
    Options opts;
    opts.no_dither = true;
    Mo5Screen screen{};
    convert(rgb.data(), opts, screen);

    for (int i = 0; i < TOTAL_BLOCKS; ++i) {
        uint8_t fg = screen.colors[i] >> 4;
        uint8_t bg = screen.colors[i] & 0x0F;
        int bx = i % BLOCKS_PER_ROW;
        for (int bit = 7; bit >= 0; --bit) {
            uint8_t c = ((screen.pixels[i] >> bit) & 1) ? fg : bg;
            if (bx % 2 == 0) {
                REQUIRE(c == 1);  // red
            } else {
                REQUIRE(c == 4);  // blue
            }
        }
    }
}

TEST_CASE("convert — no-dither mode produces no error leakage") {
    std::vector<uint8_t> rgb(MO5_W * MO5_H * 3);
    // Left half red, right half blue
    for (int y = 0; y < MO5_H; ++y) {
        for (int x = 0; x < MO5_W; ++x) {
            int off = (y * MO5_W + x) * 3;
            if (x < MO5_W / 2) {
                rgb[off] = 255; rgb[off + 1] = 0; rgb[off + 2] = 0;
            } else {
                rgb[off] = 0; rgb[off + 1] = 0; rgb[off + 2] = 255;
            }
        }
    }
    Options opts;
    opts.no_dither = true;
    Mo5Screen screen{};
    convert(rgb.data(), opts, screen);

    for (int i = 0; i < TOTAL_BLOCKS; ++i) {
        uint8_t fg = screen.colors[i] >> 4;
        uint8_t bg = screen.colors[i] & 0x0F;
        int bx = i % BLOCKS_PER_ROW;
        for (int bit = 7; bit >= 0; --bit) {
            uint8_t c = ((screen.pixels[i] >> bit) & 1) ? fg : bg;
            if (bx < 20) {
                REQUIRE(c == 1);  // red
            } else {
                REQUIRE(c == 4);  // blue
            }
        }
    }
}

// ============================================================================
// 10.7  Output Tests
// ============================================================================

TEST_CASE("write_bin — round-trip") {
    uint8_t data[8000];
    for (int i = 0; i < 8000; ++i) data[i] = static_cast<uint8_t>(i & 0xFF);

    const char* tmp = "test_roundtrip.bin";
    write_bin(tmp, data, 8000);

    FILE* f = std::fopen(tmp, "rb");
    REQUIRE(f != nullptr);
    uint8_t read_data[8000];
    REQUIRE(std::fread(read_data, 1, 8000, f) == 8000);
    std::fclose(f);

    REQUIRE(std::memcmp(data, read_data, 8000) == 0);
    std::remove(tmp);
}

TEST_CASE("write_preview — pixel-accurate rendering") {
    Palette pal = init_palette();
    Mo5Screen screen{};
    std::memset(&screen, 0, sizeof(screen));

    // offset 0: fg=1 (red), bg=2 (green), pixels=0b10000001
    screen.colors[0] = (1 << 4) | 2;
    screen.pixels[0] = 0x81;

    const char* tmp = "test_preview.png";
    write_preview(tmp, screen, pal);

    int w, h, ch;
    uint8_t* img = stbi_load(tmp, &w, &h, &ch, 3);
    REQUIRE(img != nullptr);
    REQUIRE(w == 320);
    REQUIRE(h == 200);

    // Pixel 0 (bit 7 = 1 → fg = red)
    REQUIRE(img[0] == 255); REQUIRE(img[1] == 0); REQUIRE(img[2] == 0);
    // Pixel 7 (bit 0 = 1 → fg = red)
    REQUIRE(img[7 * 3] == 255); REQUIRE(img[7 * 3 + 1] == 0); REQUIRE(img[7 * 3 + 2] == 0);
    // Pixel 1 (bit 6 = 0 → bg = green)
    REQUIRE(img[1 * 3] == 0); REQUIRE(img[1 * 3 + 1] == 255); REQUIRE(img[1 * 3 + 2] == 0);
    // Pixel 6 (bit 1 = 0 → bg = green)
    REQUIRE(img[6 * 3] == 0); REQUIRE(img[6 * 3 + 1] == 255); REQUIRE(img[6 * 3 + 2] == 0);

    stbi_image_free(img);
    std::remove(tmp);
}

// ============================================================================
// 10.9  CLI Argument Parsing
// ============================================================================

TEST_CASE("parse_args — minimal") {
    char* argv[] = {(char*)"png2mo5", (char*)"photo.png"};
    Options opts = parse_args(2, argv);
    REQUIRE(opts.input_path == "photo.png");
    REQUIRE(opts.output_path == "photo");
    REQUIRE(!opts.nearest);
    REQUIRE(!opts.no_dither);
    REQUIRE(!opts.lab_mode);
}

TEST_CASE("parse_args — full flags") {
    char* argv[] = {(char*)"png2mo5", (char*)"dir/photo.jpg", (char*)"-o", (char*)"out",
                    (char*)"--nearest", (char*)"--no-dither", (char*)"--lab"};
    Options opts = parse_args(7, argv);
    REQUIRE(opts.input_path == "dir/photo.jpg");
    REQUIRE(opts.output_path == "out");
    REQUIRE(opts.nearest);
    REQUIRE(opts.no_dither);
    REQUIRE(opts.lab_mode);
}

TEST_CASE("parse_args — basename derived from input path") {
    char* argv1[] = {(char*)"png2mo5", (char*)"/some/path/image.bmp"};
    Options opts1 = parse_args(2, argv1);
    REQUIRE(opts1.output_path == "image");

    char* argv2[] = {(char*)"png2mo5", (char*)"file.with.dots.png"};
    Options opts2 = parse_args(2, argv2);
    REQUIRE(opts2.output_path == "file.with.dots");
}

TEST_CASE("parse_args — flags in any order") {
    char* argv[] = {(char*)"png2mo5", (char*)"--no-dither", (char*)"input.png", (char*)"--nearest"};
    Options opts = parse_args(4, argv);
    REQUIRE(opts.input_path == "input.png");
    REQUIRE(opts.nearest);
    REQUIRE(opts.no_dither);
}

TEST_CASE("parse_args — --damping sets value") {
    char* argv[] = {(char*)"png2mo5", (char*)"input.png", (char*)"--damping", (char*)"0.5"};
    Options opts = parse_args(4, argv);
    REQUIRE(opts.error_damping == Catch::Approx(0.5f));
}

TEST_CASE("parse_args — --damping out of range throws") {
    char* argv[] = {(char*)"png2mo5", (char*)"input.png", (char*)"--damping", (char*)"1.5"};
    REQUIRE_THROWS_AS(parse_args(4, argv), std::runtime_error);
}

TEST_CASE("parse_args — --damping missing value throws") {
    char* argv[] = {(char*)"png2mo5", (char*)"input.png", (char*)"--damping"};
    REQUIRE_THROWS_AS(parse_args(3, argv), std::runtime_error);
}

TEST_CASE("parse_args — unknown flag throws") {
    char* argv[] = {(char*)"png2mo5", (char*)"input.png", (char*)"--bogus"};
    REQUIRE_THROWS_AS(parse_args(3, argv), std::runtime_error);
}

TEST_CASE("parse_args — no input throws") {
    char* argv[] = {(char*)"png2mo5"};
    REQUIRE_THROWS_AS(parse_args(1, argv), std::runtime_error);
}

// ============================================================================
// 10.10  Compile-Time Pair Table Tests
// ============================================================================

TEST_CASE("ALL_PAIRS has 136 entries") {
    REQUIRE(ALL_PAIRS.size() == 136);
}

TEST_CASE("ALL_PAIRS contains all valid pairs") {
    // Every pair has c1 <= c2
    for (const auto& p : ALL_PAIRS) {
        REQUIRE(p.c1 <= p.c2);
    }

    // No duplicates, all combos present
    int count = 0;
    for (int i = 0; i <= 15; ++i) {
        for (int j = i; j <= 15; ++j) {
            bool found = false;
            for (const auto& p : ALL_PAIRS) {
                if (p.c1 == i && p.c2 == j) { found = true; break; }
            }
            REQUIRE(found);
            ++count;
        }
    }
    REQUIRE(count == 136);
}

// ============================================================================
// 10.11  End-to-End Test
// ============================================================================

TEST_CASE("end-to-end — solid color round-trip") {
    // All red
    std::vector<uint8_t> rgb(MO5_W * MO5_H * 3);
    for (int i = 0; i < MO5_W * MO5_H; ++i) {
        rgb[i * 3]     = 255;
        rgb[i * 3 + 1] = 0;
        rgb[i * 3 + 2] = 0;
    }

    Options opts;
    opts.no_dither = false;
    Mo5Screen screen{};
    convert(rgb.data(), opts, screen);

    // Write and verify binary output
    write_bin("test_e2e_pixels.bin", screen.pixels, 8000);
    write_bin("test_e2e_colors.bin", screen.colors, 8000);

    // Every pixel should render to red (palette index 1)
    for (int i = 0; i < 8000; ++i) {
        uint8_t fg = screen.colors[i] >> 4;
        uint8_t bg = screen.colors[i] & 0x0F;
        for (int bit = 7; bit >= 0; --bit) {
            uint8_t c = ((screen.pixels[i] >> bit) & 1) ? fg : bg;
            REQUIRE(c == 1);
        }
    }

    // Write and verify preview
    Palette pal = init_palette();
    write_preview("test_e2e_preview.png", screen, pal);

    int w, h, ch;
    uint8_t* img = stbi_load("test_e2e_preview.png", &w, &h, &ch, 3);
    REQUIRE(img != nullptr);
    for (int i = 0; i < MO5_W * MO5_H; ++i) {
        REQUIRE(img[i * 3] == 255);
        REQUIRE(img[i * 3 + 1] == 0);
        REQUIRE(img[i * 3 + 2] == 0);
    }
    stbi_image_free(img);

    std::remove("test_e2e_pixels.bin");
    std::remove("test_e2e_colors.bin");
    std::remove("test_e2e_preview.png");
}
