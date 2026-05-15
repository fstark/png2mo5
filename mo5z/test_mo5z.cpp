#define MO5Z_TESTING
#include "mo5z.cpp"
#include "catch2/catch_amalgamated.hpp"

#include <numeric>
#include <random>

// ═══════════════════════════════════════════════════════════════════════════════
// Reorder Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("reorder_col_to_row — all zeros", "[reorder]") {
    std::vector<uint8_t> col(8000, 0);
    auto row = reorder_col_to_row(col);
    REQUIRE(row == col);
}

TEST_CASE("reorder_col_to_row — known pattern", "[reorder]") {
    std::vector<uint8_t> col(8000);
    for (int c = 0; c < 40; c++)
        for (int r = 0; r < 200; r++)
            col[c * 200 + r] = static_cast<uint8_t>((c * 200 + r) & 0xFF);

    auto row = reorder_col_to_row(col);
    for (int c = 0; c < 40; c++)
        for (int r = 0; r < 200; r++)
            REQUIRE(row[r * 40 + c] == static_cast<uint8_t>((c * 200 + r) & 0xFF));
}

TEST_CASE("reorder round-trip", "[reorder]") {
    std::vector<uint8_t> original(8000);
    std::iota(original.begin(), original.end(), 0);
    auto col = reorder_row_to_col(original);
    auto row = reorder_col_to_row(col);
    REQUIRE(row == original);
}

TEST_CASE("reorder_row_to_col inverse", "[reorder]") {
    std::vector<uint8_t> original(8000);
    std::iota(original.begin(), original.end(), 42);
    auto col = reorder_row_to_col(original);
    auto back = reorder_col_to_row(col);
    REQUIRE(back == original);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Nibble Packing Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("pack_nibble_pairs — basic", "[nibble]") {
    std::vector<uint8_t> input = {0x0A, 0x0B, 0x0C, 0x0D};
    auto packed = pack_nibble_pairs(input);
    REQUIRE(packed.size() == 2);
    REQUIRE(packed[0] == 0xAB);
    REQUIRE(packed[1] == 0xCD);
}

TEST_CASE("pack_nibble_pairs — all zeros", "[nibble]") {
    std::vector<uint8_t> input(8000, 0);
    auto packed = pack_nibble_pairs(input);
    REQUIRE(packed.size() == 4000);
    REQUIRE(packed == std::vector<uint8_t>(4000, 0));
}

TEST_CASE("pack_nibble_pairs — all 0x0F", "[nibble]") {
    std::vector<uint8_t> input(8000, 0x0F);
    auto packed = pack_nibble_pairs(input);
    REQUIRE(packed.size() == 4000);
    REQUIRE(packed == std::vector<uint8_t>(4000, 0xFF));
}

TEST_CASE("unpack_nibble_pairs — basic", "[nibble]") {
    std::vector<uint8_t> packed = {0xAB, 0xCD};
    auto unpacked = unpack_nibble_pairs(packed);
    REQUIRE(unpacked.size() == 4);
    REQUIRE(unpacked[0] == 0x0A);
    REQUIRE(unpacked[1] == 0x0B);
    REQUIRE(unpacked[2] == 0x0C);
    REQUIRE(unpacked[3] == 0x0D);
}

TEST_CASE("pack/unpack round-trip", "[nibble]") {
    std::mt19937 rng(12345);
    std::vector<uint8_t> nibbles(8000);
    for (auto& n : nibbles) n = rng() & 0x0F;
    auto packed = pack_nibble_pairs(nibbles);
    auto unpacked = unpack_nibble_pairs(packed);
    REQUIRE(unpacked == nibbles);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Free-Nibble (Solid Block) Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("solve_free_nibble — visible matches prev_n1 only", "[free_nibble]") {
    auto nc = solve_free_nibble(3, 3, 5);
    REQUIRE(nc.pix == 0xFF);
    REQUIRE(nc.n1 == 3);
    REQUIRE(nc.n2 == 5);
}

TEST_CASE("solve_free_nibble — visible matches prev_n2 only", "[free_nibble]") {
    auto nc = solve_free_nibble(5, 3, 5);
    REQUIRE(nc.pix == 0x00);
    REQUIRE(nc.n1 == 3);
    REQUIRE(nc.n2 == 5);
}

TEST_CASE("solve_free_nibble — visible matches both", "[free_nibble]") {
    auto nc = solve_free_nibble(7, 7, 7);
    REQUIRE(nc.pix == 0x00);
    REQUIRE(nc.n1 == 7);
    REQUIRE(nc.n2 == 7);
}

TEST_CASE("solve_free_nibble — visible matches neither", "[free_nibble]") {
    auto nc = solve_free_nibble(9, 3, 5);
    REQUIRE(nc.pix == 0x00);
    REQUIRE(nc.n1 == 3);
    REQUIRE(nc.n2 == 9);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Visual Equivalence Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("blocks_visually_equal — identical blocks", "[visual]") {
    REQUIRE(blocks_visually_equal(0xA5, 3, 7, 0xA5, 3, 7));
}

TEST_CASE("blocks_visually_equal — swapped orientation", "[visual]") {
    uint8_t pix = 0xA5;
    REQUIRE(blocks_visually_equal(pix, 3, 7, pix ^ 0xFF, 7, 3));
}

TEST_CASE("blocks_visually_equal — solid-zero with any fg", "[visual]") {
    REQUIRE(blocks_visually_equal(0x00, 1, 5, 0x00, 9, 5));
}

TEST_CASE("blocks_visually_equal — solid-FF with any bg", "[visual]") {
    REQUIRE(blocks_visually_equal(0xFF, 3, 1, 0xFF, 3, 12));
}

TEST_CASE("blocks_visually_equal — actually different", "[visual]") {
    REQUIRE_FALSE(blocks_visually_equal(0xA5, 3, 7, 0xA5, 3, 8));
}

// ═══════════════════════════════════════════════════════════════════════════════
// Canonicalization Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("canonicalize — first block in column uses min/max rule", "[canon]") {
    std::vector<uint8_t> pixels(8000, 0x55);
    std::vector<uint8_t> colors(8000, 0);

    // Set column 0, row 0: fg=9, bg=2 → should orient as n1=2, n2=9
    colors[0] = (9 << 4) | 2;

    std::vector<uint8_t> out_pix, out_fg, out_bg;
    canonicalize_and_reorder(pixels.data(), colors.data(), out_pix, out_fg, out_bg);

    // Column 0, row 0 is at col_major index 0
    REQUIRE(out_fg[0] <= out_bg[0]);
    REQUIRE(out_fg[0] == 2);
    REQUIRE(out_bg[0] == 9);
    // Pixels should be inverted since we swapped
    REQUIRE(out_pix[0] == (uint8_t)(0x55 ^ 0xFF));
}

TEST_CASE("canonicalize — swap decision prefers continuity", "[canon]") {
    std::vector<uint8_t> pixels(8000, 0x55); // non-solid
    std::vector<uint8_t> colors(8000, 0);

    // Col 0 row 0: fg=2 bg=5 → min/max → n1=2 n2=5 pix=0x55
    colors[0 * 40 + 0] = (2 << 4) | 5;
    // Col 0 row 1: fg=5 bg=2 → direct would be (5,2) cost=2, swapped (2,5) cost=0
    colors[1 * 40 + 0] = (5 << 4) | 2;

    std::vector<uint8_t> out_pix, out_fg, out_bg;
    canonicalize_and_reorder(pixels.data(), colors.data(), out_pix, out_fg, out_bg);

    // Col 0 row 1 at col_major index 1
    REQUIRE(out_fg[1] == 2);
    REQUIRE(out_bg[1] == 5);
    REQUIRE(out_pix[1] == (uint8_t)(0x55 ^ 0xFF));
}

TEST_CASE("canonicalize — solid-zero block uses free nibble", "[canon]") {
    std::vector<uint8_t> pixels(8000, 0x55);
    std::vector<uint8_t> colors(8000, 0);

    // Col 0 row 0: fg=3 bg=5 → n1=3 n2=5 (min/max)
    colors[0 * 40 + 0] = (3 << 4) | 5;
    pixels[0 * 40 + 0] = 0x55;

    // Col 0 row 1: pix=0x00, visible color is bg=3, fg=7
    // visible=3 matches prev_n1=3, so should emit {0xFF, 3, 5}
    colors[1 * 40 + 0] = (7 << 4) | 3;
    pixels[1 * 40 + 0] = 0x00;

    std::vector<uint8_t> out_pix, out_fg, out_bg;
    canonicalize_and_reorder(pixels.data(), colors.data(), out_pix, out_fg, out_bg);

    REQUIRE(out_pix[1] == 0xFF);
    REQUIRE(out_fg[1] == 3);
    REQUIRE(out_bg[1] == 5);
}

TEST_CASE("canonicalize — solid-FF block uses free nibble", "[canon]") {
    std::vector<uint8_t> pixels(8000, 0x55);
    std::vector<uint8_t> colors(8000, 0);

    // Col 0 row 0: fg=3 bg=5 → n1=3 n2=5 (min/max)
    colors[0 * 40 + 0] = (3 << 4) | 5;
    pixels[0 * 40 + 0] = 0x55;

    // Col 0 row 1: pix=0xFF, visible color is fg=5, bg=9
    // visible=5 matches prev_n2=5, so should emit {0x00, 3, 5}
    colors[1 * 40 + 0] = (5 << 4) | 9;
    pixels[1 * 40 + 0] = 0xFF;

    std::vector<uint8_t> out_pix, out_fg, out_bg;
    canonicalize_and_reorder(pixels.data(), colors.data(), out_pix, out_fg, out_bg);

    REQUIRE(out_pix[1] == 0x00);
    REQUIRE(out_fg[1] == 3);
    REQUIRE(out_bg[1] == 5);
}

TEST_CASE("canonicalize — preserves visual equivalence", "[canon]") {
    std::mt19937 rng(42);
    std::vector<uint8_t> pixels(8000), colors(8000);
    for (auto& p : pixels) p = rng() & 0xFF;
    for (auto& c : colors) c = rng() & 0xFF;

    std::vector<uint8_t> out_pix, out_fg, out_bg;
    canonicalize_and_reorder(pixels.data(), colors.data(), out_pix, out_fg, out_bg);

    // Reorder back to row-major for comparison
    auto row_pix = reorder_col_to_row(out_pix);
    auto row_fg  = reorder_col_to_row(out_fg);
    auto row_bg  = reorder_col_to_row(out_bg);

    for (int i = 0; i < 8000; i++) {
        uint8_t orig_fg_nib = (colors[i] >> 4) & 0x0F;
        uint8_t orig_bg_nib = colors[i] & 0x0F;
        REQUIRE(blocks_visually_equal(pixels[i], orig_fg_nib, orig_bg_nib,
                                      row_pix[i], row_fg[i], row_bg[i]));
    }
}

TEST_CASE("canonicalize — output is column-major", "[canon]") {
    std::vector<uint8_t> pixels(8000, 0x55);
    std::vector<uint8_t> colors(8000);
    // Unique color per block for identification
    for (int i = 0; i < 8000; i++) {
        uint8_t n1 = (i % 15);
        uint8_t n2 = (i % 15) + 1;
        colors[i] = (n1 << 4) | n2;
    }

    std::vector<uint8_t> out_pix, out_fg, out_bg;
    canonicalize_and_reorder(pixels.data(), colors.data(), out_pix, out_fg, out_bg);

    // Index 0 should be col=0, row=0, index 1 should be col=0, row=1
    // Index 200 should be col=1, row=0
    REQUIRE(out_pix.size() == 8000);
    // Verify first column entries correspond to rows 0..199 of col 0
    // and col 1 starts at index 200
    // (Basic structural check — just verify sizes and that reorder back works)
    auto row = reorder_col_to_row(out_pix);
    REQUIRE(row.size() == 8000);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Full Pipeline (build_streams) Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("build_streams — all-black screen", "[pipeline]") {
    std::vector<uint8_t> pixels(8000, 0x00);
    std::vector<uint8_t> colors(8000, 0x00); // fg=0, bg=0

    Streams s = build_streams(pixels.data(), colors.data());
    REQUIRE(s.pixels.size() == 8000);
    REQUIRE(s.fg_packed.size() == 4000);
    REQUIRE(s.bg_packed.size() == 4000);
}

TEST_CASE("build_streams — pixel stream is 8000 bytes", "[pipeline]") {
    std::mt19937 rng(99);
    std::vector<uint8_t> pixels(8000), colors(8000);
    for (auto& p : pixels) p = rng();
    for (auto& c : colors) c = rng();

    Streams s = build_streams(pixels.data(), colors.data());
    REQUIRE(s.pixels.size() == 8000);
}

TEST_CASE("build_streams — fg/bg packed are 4000 bytes each", "[pipeline]") {
    std::mt19937 rng(77);
    std::vector<uint8_t> pixels(8000), colors(8000);
    for (auto& p : pixels) p = rng();
    for (auto& c : colors) c = rng();

    Streams s = build_streams(pixels.data(), colors.data());
    REQUIRE(s.fg_packed.size() == 4000);
    REQUIRE(s.bg_packed.size() == 4000);
}

// ═══════════════════════════════════════════════════════════════════════════════
// ZX0 Compress/Decompress Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("zx0_compress — non-empty output", "[zx0]") {
    std::vector<uint8_t> data(8000, 0);
    auto compressed = zx0_compress(data);
    REQUIRE(compressed.size() > 0);
    REQUIRE(compressed.size() < data.size());
}

TEST_CASE("zx0_decompress — round-trip zeros", "[zx0]") {
    std::vector<uint8_t> data(8000, 0);
    auto compressed = zx0_compress(data);
    auto decompressed = zx0_decompress(compressed.data(), compressed.size(), 8000);
    REQUIRE(decompressed == data);
}

TEST_CASE("zx0_decompress — round-trip random", "[zx0]") {
    std::mt19937 rng(555);
    std::vector<uint8_t> data(4000);
    for (auto& b : data) b = rng();
    auto compressed = zx0_compress(data);
    auto decompressed = zx0_decompress(compressed.data(), compressed.size(), 4000);
    REQUIRE(decompressed == data);
}

TEST_CASE("zx0_decompress — round-trip repetitive", "[zx0]") {
    std::vector<uint8_t> data(4000);
    for (size_t i = 0; i < data.size(); i++) data[i] = (i / 50) & 0xFF;
    auto compressed = zx0_compress(data);
    auto decompressed = zx0_decompress(compressed.data(), compressed.size(), 4000);
    REQUIRE(decompressed == data);
}

TEST_CASE("zx0_decompress — wrong expected_size throws", "[zx0]") {
    std::vector<uint8_t> data(1000, 0x42);
    auto compressed = zx0_compress(data);
    REQUIRE_THROWS_AS(zx0_decompress(compressed.data(), compressed.size(), 999), std::runtime_error);
}

TEST_CASE("zx0_decompress — truncated data throws", "[zx0]") {
    std::vector<uint8_t> data(1000, 0x42);
    auto compressed = zx0_compress(data);
    // Feed only half the compressed data
    REQUIRE_THROWS_AS(zx0_decompress(compressed.data(), compressed.size() / 2, 1000), std::runtime_error);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Verification Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("verify — passes for valid compression", "[verify]") {
    std::mt19937 rng(123);
    std::vector<uint8_t> pixels(8000), colors(8000);
    for (auto& p : pixels) p = rng();
    for (auto& c : colors) c = rng();

    Streams streams = build_streams(pixels.data(), colors.data());
    CompressedStreams compressed;
    compressed.pixels_zx0 = zx0_compress(streams.pixels);
    compressed.fg_zx0     = zx0_compress(streams.fg_packed);
    compressed.bg_zx0     = zx0_compress(streams.bg_packed);

    REQUIRE_NOTHROW(verify(streams, compressed, pixels.data(), colors.data()));
}

TEST_CASE("verify — detects corruption in pixel stream", "[verify]") {
    std::vector<uint8_t> pixels(8000, 0x55);
    std::vector<uint8_t> colors(8000, 0x37);

    Streams streams = build_streams(pixels.data(), colors.data());
    CompressedStreams compressed;
    compressed.pixels_zx0 = zx0_compress(streams.pixels);
    compressed.fg_zx0     = zx0_compress(streams.fg_packed);
    compressed.bg_zx0     = zx0_compress(streams.bg_packed);

    // Corrupt compressed pixel stream
    if (compressed.pixels_zx0.size() > 2)
        compressed.pixels_zx0[1] ^= 0xFF;

    REQUIRE_THROWS_AS(verify(streams, compressed, pixels.data(), colors.data()), std::runtime_error);
}

TEST_CASE("verify — detects corruption in fg stream", "[verify]") {
    std::vector<uint8_t> pixels(8000, 0x55);
    std::vector<uint8_t> colors(8000, 0x37);

    Streams streams = build_streams(pixels.data(), colors.data());
    CompressedStreams compressed;
    compressed.pixels_zx0 = zx0_compress(streams.pixels);
    compressed.fg_zx0     = zx0_compress(streams.fg_packed);
    compressed.bg_zx0     = zx0_compress(streams.bg_packed);

    if (compressed.fg_zx0.size() > 2)
        compressed.fg_zx0[1] ^= 0xFF;

    REQUIRE_THROWS_AS(verify(streams, compressed, pixels.data(), colors.data()), std::runtime_error);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Integration Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("full pipeline — synthetic gradient", "[integration]") {
    std::vector<uint8_t> pixels(8000), colors(8000);
    for (int i = 0; i < 8000; i++) {
        pixels[i] = (i * 7) & 0xFF;
        colors[i] = ((i % 16) << 4) | ((i + 3) % 16);
    }

    Streams streams = build_streams(pixels.data(), colors.data());
    CompressedStreams compressed;
    compressed.pixels_zx0 = zx0_compress(streams.pixels);
    compressed.fg_zx0     = zx0_compress(streams.fg_packed);
    compressed.bg_zx0     = zx0_compress(streams.bg_packed);

    REQUIRE_NOTHROW(verify(streams, compressed, pixels.data(), colors.data()));
    REQUIRE(compressed.pixels_zx0.size() > 0);
    REQUIRE(compressed.fg_zx0.size() > 0);
    REQUIRE(compressed.bg_zx0.size() > 0);
}

TEST_CASE("full pipeline — worst case random", "[integration]") {
    std::mt19937 rng(999);
    std::vector<uint8_t> pixels(8000), colors(8000);
    for (auto& p : pixels) p = rng();
    for (auto& c : colors) c = rng();

    Streams streams = build_streams(pixels.data(), colors.data());
    CompressedStreams compressed;
    compressed.pixels_zx0 = zx0_compress(streams.pixels);
    compressed.fg_zx0     = zx0_compress(streams.fg_packed);
    compressed.bg_zx0     = zx0_compress(streams.bg_packed);

    REQUIRE_NOTHROW(verify(streams, compressed, pixels.data(), colors.data()));
}

TEST_CASE("full pipeline — all-same-color", "[integration]") {
    std::vector<uint8_t> pixels(8000, 0x00);
    std::vector<uint8_t> colors(8000, 0x35); // fg=3, bg=5

    Streams streams = build_streams(pixels.data(), colors.data());
    CompressedStreams compressed;
    compressed.pixels_zx0 = zx0_compress(streams.pixels);
    compressed.fg_zx0     = zx0_compress(streams.fg_packed);
    compressed.bg_zx0     = zx0_compress(streams.bg_packed);

    REQUIRE_NOTHROW(verify(streams, compressed, pixels.data(), colors.data()));

    // Highly repetitive data should compress very well
    size_t total_compressed = compressed.pixels_zx0.size() + compressed.fg_zx0.size() + compressed.bg_zx0.size();
    REQUIRE(total_compressed < 500); // should be tiny for uniform data
}

// ═══════════════════════════════════════════════════════════════════════════════
// I/O Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("read_bin — wrong size throws", "[io]") {
    // Create a temp file with wrong size
    const char* path = "/tmp/mo5z_test_wrong_size.bin";
    FILE* f = fopen(path, "wb");
    REQUIRE(f != nullptr);
    std::vector<uint8_t> data(7999, 0);
    fwrite(data.data(), 1, data.size(), f);
    fclose(f);

    REQUIRE_THROWS_AS(read_bin(path, 8000), std::runtime_error);
    remove(path);
}

TEST_CASE("read_bin — missing file throws", "[io]") {
    REQUIRE_THROWS_AS(read_bin("/tmp/mo5z_nonexistent_file_xyz.bin", 8000), std::runtime_error);
}

TEST_CASE("write_output + read back", "[io]") {
    CompressedStreams c;
    c.pixels_zx0 = {1, 2, 3, 4, 5};
    c.fg_zx0 = {6, 7, 8};
    c.bg_zx0 = {9, 10};

    const char* path = "/tmp/mo5z_test_output.mo5z";
    write_output(c, path);

    FILE* f = fopen(path, "rb");
    REQUIRE(f != nullptr);
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    REQUIRE(size == 10);

    std::vector<uint8_t> read_back(size);
    fread(read_back.data(), 1, size, f);
    fclose(f);

    std::vector<uint8_t> expected = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    REQUIRE(read_back == expected);
    remove(path);
}
