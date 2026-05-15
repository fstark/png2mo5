# mo5z — Implementation Plan

## 1. Directory Layout

```
mo5z/
├── DESIGN.md              (exists)
├── PLAN.md                (this file)
├── Makefile
├── mo5z.cpp               (main source — single-file, with #ifdef for testing)
├── test_mo5z.cpp          (Catch2 tests — includes mo5z.cpp with MO5Z_TESTING)
├── catch2/
│   ├── catch_amalgamated.cpp   (symlink or copy from ../png2mo5/catch2/)
│   └── catch_amalgamated.hpp   (symlink or copy from ../png2mo5/catch2/)
└── zx0/
    ├── zx0.h              (vendored from github.com/einar-saukas/ZX0/src/)
    ├── compress.c
    ├── optimize.c
    └── memory.c
```

---

## 2. Build System (Makefile)

```makefile
CXX      ?= g++
CC       ?= gcc
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
CFLAGS   ?= -std=c11 -O2 -Wall -Wextra

ZX0_SRCS = zx0/compress.c zx0/optimize.c zx0/memory.c
ZX0_OBJS = $(ZX0_SRCS:.c=.o)

mo5z: mo5z.cpp $(ZX0_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $< $(ZX0_OBJS)

test_mo5z: test_mo5z.cpp mo5z.cpp $(ZX0_OBJS) catch2/catch_amalgamated.cpp
	$(CXX) $(CXXFLAGS) -DMO5Z_TESTING -o $@ test_mo5z.cpp catch2/catch_amalgamated.cpp $(ZX0_OBJS)

zx0/%.o: zx0/%.c zx0/zx0.h
	$(CC) $(CFLAGS) -c -o $@ $<

test: test_mo5z
	./test_mo5z

clean:
	rm -f mo5z test_mo5z zx0/*.o

.PHONY: test clean
```

---

## 3. Source Structure (mo5z.cpp)

All logic lives in `mo5z.cpp`. When compiled with `-DMO5Z_TESTING`, `main()` is excluded so tests can link against the same translation unit.

### 3.1 Includes & Constants

```cpp
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

constexpr int MO5_W        = 320;
constexpr int MO5_H        = 200;
constexpr int COLS          = MO5_W / 8;  // 40
constexpr int ROWS          = MO5_H;      // 200
constexpr int TOTAL_BLOCKS  = COLS * ROWS; // 8000
constexpr int NIBBLE_STREAM_SIZE = 4000;   // 4000 nibbles → 4000 packed bytes (pair packing)
```

### 3.2 Data Structures

```cpp
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
```

---

## 4. Function Decomposition

Each function is small, testable, and does one thing.

### 4.1 `parse_args(int argc, char* argv[]) → Options`

- Accepts: `<pixels.bin> <colors.bin> [-o <stem>]`
- Validates exactly 2 positional args + optional `-o`
- On error: prints usage to stderr, exits 1

### 4.2 `read_bin(const std::string& path, size_t expected_size) → std::vector<uint8_t>`

- Opens file in binary mode, reads all bytes.
- Throws `std::runtime_error` if file can't be opened or size ≠ expected_size.

### 4.3 `canonicalize(const uint8_t* pixels, const uint8_t* colors, uint8_t* out_pixels, uint8_t* out_fg, uint8_t* out_bg)`

Inputs: row-major 8000-byte pixel & color arrays.
Outputs: row-major 8000 canonicalized pixels, 8000 fg nibbles, 8000 bg nibbles (one nibble per block, stored in low 4 bits of each byte).

**Algorithm** (processes in column-major order for continuity, but writes to row-major indexed arrays that will later be reordered):

Actually — reading DESIGN.md again, the canonicalization processes column-by-column, top-to-bottom, and the output is already meant for column-major layout. Let's merge canonicalize + reorder into one pass.

Revised: `canonicalize_and_reorder(...)` processes columns, writes directly into column-major output buffers.

### 4.4 `canonicalize_and_reorder(...)`

```cpp
void canonicalize_and_reorder(
    const uint8_t* pixels_row_major,  // [8000] input row-major pixels
    const uint8_t* colors_row_major,  // [8000] input row-major colors (fg<<4|bg)
    std::vector<uint8_t>& out_pixels, // [8000] column-major canonicalized pixels
    std::vector<uint8_t>& out_fg,     // [8000] column-major fg nibbles  
    std::vector<uint8_t>& out_bg      // [8000] column-major bg nibbles
);
```

**Detailed algorithm:**

```
for col in 0..39:
    prev_n1 = 0xFF  (sentinel: "no previous")
    prev_n2 = 0xFF

    for row in 0..199:
        row_major_idx = row * 40 + col
        col_major_idx = col * 200 + row

        pix = pixels_row_major[row_major_idx]
        color = colors_row_major[row_major_idx]
        n_a = (color >> 4) & 0x0F   // fg nibble
        n_b = color & 0x0F          // bg nibble

        // Determine orientation
        if row == 0 (first in column):
            // Deterministic: min goes to stream 1, max to stream 2
            if n_a <= n_b:
                chosen_n1 = n_a; chosen_n2 = n_b; chosen_pix = pix
            else:
                chosen_n1 = n_b; chosen_n2 = n_a; chosen_pix = pix ^ 0xFF

        else if pix == 0x00:
            // Solid block: all pixels are bg color (n_b is visible)
            // Apply free-nibble logic
            (chosen_pix, chosen_n1, chosen_n2) = free_nibble_solid_zero(n_b, prev_n1, prev_n2)

        else if pix == 0xFF:
            // Solid block: all pixels are fg color (n_a is visible)
            (chosen_pix, chosen_n1, chosen_n2) = free_nibble_solid_ff(n_a, prev_n1, prev_n2)

        else:
            // Normal block — pick orientation minimizing cost
            // Option A: keep as-is → (n_a, n_b)
            // Option B: invert   → (n_b, n_a), pix^0xFF
            cost_a = (n_a != prev_n1) + (n_b != prev_n2)
            cost_b = (n_b != prev_n1) + (n_a != prev_n2)
            if cost_a <= cost_b:
                chosen_n1 = n_a; chosen_n2 = n_b; chosen_pix = pix
            else:
                chosen_n1 = n_b; chosen_n2 = n_a; chosen_pix = pix ^ 0xFF

        out_pixels[col_major_idx] = chosen_pix
        out_fg[col_major_idx] = chosen_n1
        out_bg[col_major_idx] = chosen_n2
        prev_n1 = chosen_n1
        prev_n2 = chosen_n2
```

### 4.5 Free-Nibble Helpers

```cpp
struct NibbleChoice {
    uint8_t pix;
    uint8_t n1;
    uint8_t n2;
};

// When pix==0x00, visible color is n_b (the bg nibble)
NibbleChoice free_nibble_solid_zero(uint8_t visible, uint8_t prev_n1, uint8_t prev_n2);

// When pix==0xFF, visible color is n_a (the fg nibble)
NibbleChoice free_nibble_solid_ff(uint8_t visible, uint8_t prev_n1, uint8_t prev_n2);
```

**Logic for `free_nibble_solid_zero` (visible = bg color, `pixels = 0x00`):**

The visible color must appear on-screen. We pick whether to put it in stream 1 or stream 2, and set the invisible nibble to maximize run continuity.

Per DESIGN.md rules:
1. If visible == prev_n1 → put visible in stream 1 (so pixels = 0xFF to make n1 the visible one). Set n2 = prev_n2.
   - Result: `{0xFF, visible, prev_n2}`
2. If visible == prev_n2 → put visible in stream 2 (pixels = 0x00). Set n1 = prev_n1.
   - Result: `{0x00, prev_n1, visible}`
3. Matches both → prefer stream 2 (pixels = 0x00).
   - Result: `{0x00, prev_n1, visible}` (same as case 2)
4. Matches neither → put in stream 2 (pixels = 0x00). n1 = prev_n1.
   - Result: `{0x00, prev_n1, visible}`

Wait — re-reading: since we receive this when the *original* `pix == 0x00`, the visible color is `n_b`. But the DESIGN says we can freely choose the representation. So:

- Check case 1: if visible == prev_n1 → flip to `{0xFF, visible, prev_n2}`
- Check case 2: if visible == prev_n2 → keep  `{0x00, prev_n1, visible}`
- If both match → keep `{0x00, prev_n1, visible}` (rule 3)
- If neither → keep `{0x00, prev_n1, visible}` (rule 4)

**Logic for `free_nibble_solid_ff` (visible = fg color, `pixels = 0xFF`):**

Same logic but for the fg color being the visible one. The visible color is `n_a`:
- Check case 1: if visible == prev_n1 → keep `{0xFF, visible, prev_n2}`
- Check case 2: if visible == prev_n2 → flip to `{0x00, prev_n1, visible}`
- If both match → keep as stream 2 → `{0x00, prev_n1, visible}` (rule 3)
- If neither → keep as stream 2 → `{0x00, prev_n1, visible}` (rule 4)

**Unified free-nibble function** (cleaner implementation):

```cpp
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
```

This single helper covers both solid-zero and solid-FF cases because by the time we call it, we've extracted the visible color regardless of orientation.

### 4.6 `pack_nibble_pairs(const std::vector<uint8_t>& nibbles) → std::vector<uint8_t>`

Packs 8000 column-major nibbles into 4000 bytes by combining vertically-adjacent pairs:
```
packed[i] = nibbles[2*i] << 4 | nibbles[2*i + 1]
```

Each column has 200 nibbles → 100 packed bytes. 40 columns × 100 = 4000 bytes.

Since the input is already column-major (200 entries per column), pairs are simply consecutive elements.

### 4.7 `build_streams(...) → Streams`

Orchestrates steps 4.4 + 4.6:
```cpp
Streams build_streams(const uint8_t* pixels, const uint8_t* colors) {
    std::vector<uint8_t> col_pixels(8000), col_fg(8000), col_bg(8000);
    canonicalize_and_reorder(pixels, colors, col_pixels, col_fg, col_bg);

    Streams s;
    s.pixels = std::move(col_pixels);
    s.fg_packed = pack_nibble_pairs(col_fg);
    s.bg_packed = pack_nibble_pairs(col_bg);
    return s;
}
```

### 4.8 `zx0_compress(const std::vector<uint8_t>& data) → std::vector<uint8_t>`

Wrapper around the vendored ZX0 C API:
```cpp
std::vector<uint8_t> zx0_compress(const std::vector<uint8_t>& data) {
    int output_size = 0;
    int delta = 0;
    BLOCK* optimal = optimize(
        const_cast<unsigned char*>(data.data()),
        static_cast<int>(data.size()),
        0,                                  // skip
        static_cast<int>(data.size())       // offset_limit = input_size (full window)
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
```

**Note:** The ZX0 `optimize()` function prints progress to stdout. We'll need to suppress that. Options:
- Redirect stdout during compression (not clean).
- Patch the vendored `optimize.c` to remove the `printf` calls (simplest, since it's vendored).

**Decision:** Patch vendored `optimize.c` to remove the progress `printf("[")`, `printf(".")`, `printf("]\n")` calls.

### 4.9 `zx0_decompress(const uint8_t* data, size_t compressed_size, size_t expected_size) → std::vector<uint8_t>`

In-memory ZX0 decompressor for verification. Reimplemented in C++ to work on buffers (the upstream `dzx0.c` works on files). This is straightforward since the format is simple:

```cpp
std::vector<uint8_t> zx0_decompress(const uint8_t* src, size_t src_size, size_t expected_size) {
    std::vector<uint8_t> out;
    out.reserve(expected_size);

    size_t src_idx = 0;
    int bit_mask = 0;
    int bit_value = 0;
    bool backtrack = false;
    int last_byte = 0;
    int last_offset = 1;  // INITIAL_OFFSET

    auto read_byte = [&]() -> int {
        if (src_idx >= src_size) throw std::runtime_error("zx0 decompress: unexpected end");
        last_byte = src[src_idx++];
        return last_byte;
    };

    auto read_bit = [&]() -> int {
        if (backtrack) { backtrack = false; return last_byte & 1; }
        bit_mask >>= 1;
        if (bit_mask == 0) { bit_mask = 128; bit_value = read_byte(); }
        return (bit_value & bit_mask) ? 1 : 0;
    };

    auto read_elias = [&](bool inverted) -> int {
        int value = 1;
        while (!read_bit()) {
            value = (value << 1) | (read_bit() ^ (inverted ? 1 : 0));
        }
        return value;
    };

    auto write_byte = [&](uint8_t b) { out.push_back(b); };

    auto write_bytes = [&](int offset, int length) {
        for (int i = 0; i < length; i++) {
            size_t idx = out.size() - offset;
            write_byte(out[idx]);
        }
    };

    // First block is always literal (first bit omitted)
    goto COPY_LITERALS;

COPY_LITERALS: {
    int length = read_elias(false);
    for (int i = 0; i < length; i++) write_byte(read_byte());
    if (read_bit()) goto COPY_FROM_NEW_OFFSET;
    // else: copy from last offset
    int len = read_elias(false);
    write_bytes(last_offset, len);
    if (!read_bit()) goto COPY_LITERALS;
    goto COPY_FROM_NEW_OFFSET;
}

COPY_FROM_NEW_OFFSET: {
    int msb = read_elias(true);  // v2 format: inverted
    if (msb == 256) goto DONE;   // EOF marker
    last_offset = msb * 128 - (read_byte() >> 1);
    backtrack = true;
    int length = read_elias(false) + 1;
    write_bytes(last_offset, length);
    if (read_bit()) goto COPY_FROM_NEW_OFFSET;
    goto COPY_LITERALS;
}

DONE:
    if (out.size() != expected_size)
        throw std::runtime_error("zx0 decompress: size mismatch");
    return out;
}
```

**Note:** Using `goto` mirrors the reference decompressor exactly and keeps the state machine clear. Alternatively, implement as a loop with an enum state variable.

### 4.10 `unpack_nibble_pairs(const std::vector<uint8_t>& packed) → std::vector<uint8_t>`

Inverse of `pack_nibble_pairs`:
```cpp
std::vector<uint8_t> unpack_nibble_pairs(const std::vector<uint8_t>& packed) {
    std::vector<uint8_t> out(packed.size() * 2);
    for (size_t i = 0; i < packed.size(); i++) {
        out[2*i]     = (packed[i] >> 4) & 0x0F;
        out[2*i + 1] = packed[i] & 0x0F;
    }
    return out;
}
```

### 4.11 `reorder_col_to_row` / `reorder_row_to_col`

Converts 8000-byte column-major buffer back to row-major (and vice versa):
```cpp
std::vector<uint8_t> reorder_col_to_row(const std::vector<uint8_t>& col_major) {
    std::vector<uint8_t> row_major(8000);
    for (int col = 0; col < 40; col++)
        for (int row = 0; row < 200; row++)
            row_major[row * 40 + col] = col_major[col * 200 + row];
    return row_major;
}

std::vector<uint8_t> reorder_row_to_col(const std::vector<uint8_t>& row_major) {
    std::vector<uint8_t> col_major(8000);
    for (int col = 0; col < 40; col++)
        for (int row = 0; row < 200; row++)
            col_major[col * 200 + row] = row_major[row * 40 + col];
    return col_major;
}
```

### 4.12 `blocks_visually_equal` — Verification Semantics

Canonicalization changes block orientation (swapping fg/bg and inverting pixels). This is a lossy transform in terms of byte identity — we cannot reconstruct the original byte representation from the compressed form. However, canonicalization preserves **visual equivalence**: for each block, the 8-pixel color output on screen is identical.

Therefore, verification checks visual equivalence per block, not byte identity. Two blocks `(pix_a, fg_a, bg_a)` and `(pix_b, fg_b, bg_b)` are equivalent iff every pixel position produces the same color index:

```cpp
bool blocks_visually_equal(uint8_t pix_a, uint8_t fg_a, uint8_t bg_a,
                           uint8_t pix_b, uint8_t fg_b, uint8_t bg_b) {
    for (int bit = 7; bit >= 0; bit--) {
        uint8_t color_a = (pix_a & (1 << bit)) ? fg_a : bg_a;
        uint8_t color_b = (pix_b & (1 << bit)) ? fg_b : bg_b;
        if (color_a != color_b) return false;
    }
    return true;
}
```

### 4.13 `verify(const Streams& streams, const CompressedStreams& compressed, const uint8_t* orig_pixels, const uint8_t* orig_colors)`

```cpp
void verify(const Streams& streams, const CompressedStreams& compressed,
            const uint8_t* orig_pixels, const uint8_t* orig_colors) {
    // Decompress
    auto dec_pixels = zx0_decompress(compressed.pixels_zx0.data(), compressed.pixels_zx0.size(), 8000);
    auto dec_fg     = zx0_decompress(compressed.fg_zx0.data(), compressed.fg_zx0.size(), 4000);
    auto dec_bg     = zx0_decompress(compressed.bg_zx0.data(), compressed.bg_zx0.size(), 4000);

    // Check decompressed matches pre-compression streams
    if (dec_pixels != streams.pixels) throw std::runtime_error("verify: pixel stream mismatch");
    if (dec_fg != streams.fg_packed) throw std::runtime_error("verify: fg stream mismatch");
    if (dec_bg != streams.bg_packed) throw std::runtime_error("verify: bg stream mismatch");

    // Unpack nibbles
    auto fg_nibbles = unpack_nibble_pairs(dec_fg);
    auto bg_nibbles = unpack_nibble_pairs(dec_bg);

    // Reorder to row-major
    auto row_pixels = reorder_col_to_row(dec_pixels);
    auto row_fg     = reorder_col_to_row(fg_nibbles);
    auto row_bg     = reorder_col_to_row(bg_nibbles);

    // Visual equivalence check
    for (int i = 0; i < 8000; i++) {
        uint8_t orig_fg = (orig_colors[i] >> 4) & 0x0F;
        uint8_t orig_bg = orig_colors[i] & 0x0F;
        if (!blocks_visually_equal(orig_pixels[i], orig_fg, orig_bg,
                                   row_pixels[i], row_fg[i], row_bg[i])) {
            throw std::runtime_error("verify: visual mismatch at block " + std::to_string(i));
        }
    }
}
```

### 4.14 `print_stats(...)`

```cpp
void print_stats(const Streams& s, const CompressedStreams& c, const std::string& output_path) {
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
```

### 4.15 `write_output(const CompressedStreams& c, const std::string& path)`

Concatenates the 3 ZX0 streams and writes to file:
```cpp
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
```

### 4.16 `main(int argc, char* argv[])`

```cpp
#ifndef MO5Z_TESTING
int main(int argc, char* argv[]) {
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
    print_stats(streams, compressed, output_path);
    return 0;
}
#endif
```

---

## 5. Test Plan (test_mo5z.cpp)

```cpp
#define MO5Z_TESTING
#include "mo5z.cpp"
#include "catch2/catch_amalgamated.hpp"
```

### 5.1 Reorder Tests

| Test Case | Description |
|-----------|-------------|
| `reorder_col_to_row identity` | All zeros → all zeros |
| `reorder_col_to_row known pattern` | Fill each column-major slot with `col*200+row`, then check `row_major[row*40+col] == col*200+row` |
| `reorder round-trip` | row→col→row→col verify no data loss |
| `reorder_row_to_col` (helper for tests) | Inverse direction, verify `col_to_row(row_to_col(x)) == x` |

### 5.2 Nibble Packing Tests

| Test Case | Description |
|-----------|-------------|
| `pack_nibble_pairs basic` | `[0x0A, 0x0B, 0x0C, 0x0D]` → `[0xAB, 0xCD]` |
| `pack_nibble_pairs all zeros` | 8000 zeros → 4000 zeros |
| `pack_nibble_pairs all 0x0F` | 8000 × 0x0F → 4000 × 0xFF |
| `unpack_nibble_pairs basic` | `[0xAB, 0xCD]` → `[0x0A, 0x0B, 0x0C, 0x0D]` |
| `pack/unpack round-trip` | Random nibbles, verify `unpack(pack(x)) == x` |

### 5.3 Free-Nibble (Solid Block) Tests

| Test Case | Description |
|-----------|-------------|
| `solve_free_nibble — visible matches prev_n1 only` | Returns `{0xFF, visible, prev_n2}` |
| `solve_free_nibble — visible matches prev_n2 only` | Returns `{0x00, prev_n1, visible}` |
| `solve_free_nibble — visible matches both` | Returns `{0x00, prev_n1, visible}` |
| `solve_free_nibble — visible matches neither` | Returns `{0x00, prev_n1, visible}` |

### 5.4 Canonicalization Tests

| Test Case | Description |
|-----------|-------------|
| `canonicalize — uniform screen (all same color)` | Should produce runs in both nibble streams |
| `canonicalize — first block in column uses min/max rule` | Verify n1 ≤ n2 for the first row of each column |
| `canonicalize — swap decision prefers continuity` | Construct two consecutive blocks where swapping the second reduces cost; verify swap happens |
| `canonicalize — solid-zero block uses free nibble` | After a block with (n1=3, n2=5), a solid-zero block with visible=3 should emit `{0xFF, 3, 5}` |
| `canonicalize — solid-FF block uses free nibble` | After (n1=3, n2=5), solid-FF with visible=5 should emit `{0x00, 3, 5}` |
| `canonicalize — preserves visual equivalence` | For all 8000 blocks, verify the screen output is identical before and after canonicalization |
| `canonicalize — output is column-major` | Index 0 corresponds to col=0,row=0; index 1 to col=0,row=1; index 200 to col=1,row=0 |

### 5.5 Full Pipeline (build_streams) Tests

| Test Case | Description |
|-----------|-------------|
| `build_streams — all-black screen` | pixels=0x00, colors=0x00 → all streams are constant |
| `build_streams — pixel stream is 8000 bytes column-major` | Verify size and ordering |
| `build_streams — fg/bg packed streams are 4000 bytes each` | Verify sizes |
| `build_streams — known small pattern` | Hand-computed expected output for a 1-column strip |

### 5.6 ZX0 Compress/Decompress Tests

| Test Case | Description |
|-----------|-------------|
| `zx0_compress — non-empty output` | Compressing 8000 zeros produces output |
| `zx0_decompress — round-trip zeros` | Compress 8000 zeros, decompress, get 8000 zeros back |
| `zx0_decompress — round-trip random` | Compress pseudo-random 4000 bytes, decompress, compare |
| `zx0_decompress — round-trip repetitive` | Typical MO5-like data with runs |
| `zx0_decompress — wrong expected_size throws` | Decompress with wrong size → exception |
| `zx0_decompress — truncated data throws` | Feed fewer bytes than valid → exception |

### 5.7 Verification Tests

| Test Case | Description |
|-----------|-------------|
| `verify — passes for valid compression` | Full pipeline on test data |
| `verify — detects corruption in pixel stream` | Flip a byte in compressed pixels → throws |
| `verify — detects corruption in fg stream` | Flip a byte → throws |
| `verify — detects corruption in bg stream` | Flip a byte → throws |

### 5.8 Visual Equivalence Tests

| Test Case | Description |
|-----------|-------------|
| `blocks_visually_equal — identical blocks` | Same pixels/fg/bg → true |
| `blocks_visually_equal — swapped orientation` | `(pix, fg, bg)` vs `(pix^0xFF, bg, fg)` → true |
| `blocks_visually_equal — solid-zero with any fg` | `(0x00, X, bg)` vs `(0x00, Y, bg)` → true (bg is always shown) |
| `blocks_visually_equal — solid-FF with any bg` | `(0xFF, fg, X)` vs `(0xFF, fg, Y)` → true (fg is always shown) |
| `blocks_visually_equal — actually different` | Different visible color → false |

### 5.9 I/O and CLI Tests

| Test Case | Description |
|-----------|-------------|
| `parse_args — minimal valid` | `mo5z pixels.bin colors.bin` → correct Options |
| `parse_args — with -o flag` | `mo5z pixels.bin colors.bin -o myfile` → stem = "myfile" |
| `parse_args — missing args` | Fewer than 2 positional → exits/throws |
| `read_bin — wrong size` | File of 7999 bytes → throws |
| `read_bin — missing file` | Non-existent path → throws |
| `write_output + read back` | Write compressed data, read it back, compare bytes |

### 5.10 Integration Test

| Test Case | Description |
|-----------|-------------|
| `full pipeline — synthetic gradient` | Generate a gradient image's pixel/color banks, run full pipeline start-to-finish, verify output file is non-empty and verify passes |
| `full pipeline — worst case (random)` | Random pixels & colors, verify pipeline doesn't crash and verify passes |
| `full pipeline — all-same-color` | Uniform screen, expect high compression ratio |

---

## 6. Vendoring ZX0

### Files to vendor (into `mo5z/zx0/`):

From `github.com/einar-saukas/ZX0/src/`:
- `zx0.h`
- `compress.c`
- `optimize.c`
- `memory.c`

### Patches to vendored code:

1. **Remove progress printing from `optimize.c`:**
   - Delete the `printf("[")` line
   - Delete the `printf(".")` + `fflush(stdout)` block
   - Delete the `printf("]\n")` line

2. **No other modifications.** The `compress()` and `optimize()` APIs are used directly.

---

## 7. Implementation Order

1. Vendor ZX0 sources + patch
2. Write `Makefile`
3. Implement helper functions bottom-up:
   - `reorder_col_to_row` (and a test helper `reorder_row_to_col`)
   - `pack_nibble_pairs` / `unpack_nibble_pairs`
   - `solve_free_nibble`
   - `blocks_visually_equal`
   - `canonicalize_and_reorder`
   - `build_streams`
   - `zx0_compress` (wrapper)
   - `zx0_decompress` (in-memory reimplementation)
   - `verify`
   - `read_bin` / `write_output`
   - `parse_args`
   - `print_stats`
   - `main`
4. Write tests parallel with each function
5. `make test` passes
6. End-to-end manual test with real png2mo5 output

---

## 8. Edge Cases & Invariants

| Invariant | Check |
|-----------|-------|
| Pixel stream is always exactly 8000 bytes | `assert(streams.pixels.size() == 8000)` |
| Each packed nibble stream is exactly 4000 bytes | `assert(streams.fg_packed.size() == 4000)` |
| Nibble values are 0–15 | Debug assert in canonicalize |
| Column-major index `col*200+row` never exceeds 7999 | By construction (col<40, row<200) |
| ZX0 decompressor produces exactly expected_size bytes | Checked in `zx0_decompress` |
| All 3 streams self-terminate (ZX0 EOF marker) | Decompressor returns cleanly |
| Output .mo5z can be read by a sequential consumer | Streams are concatenated with no delimiter (self-terminating) |
| `solve_free_nibble` always sets visible in exactly one stream | By construction |
| First block in each column: `n1 <= n2` | Enforced by min/max rule |

---

## 9. Notes on ZX0 Integration

- The ZX0 `optimize()` function uses `malloc` internally for the block chain. There's no cleanup function — blocks live until process exit. For a short-lived CLI tool this is acceptable.
- `compress()` returns `malloc`'d memory — we `free()` it after copying to `std::vector`.
- ZX0's `BLOCK* optimize(...)` returns a reverse-linked-list of optimal parse decisions. `compress()` reverses it and generates the bitstream. This is the standard two-pass optimal LZ approach.
- The offset limit for MO5 data (max 8000 bytes per stream) is well within ZX0's capability.

---

## 10. Decompressor Conformance

The in-memory decompressor in §4.9 must exactly match the behavior of the reference `dzx0.c` for v2 format (non-classic, non-backwards).

**ZX0 v2 parameters:** `backwards_mode = 0`, `invert_mode = 1` (set by `zx0.c` main as `!classic_mode`).

Key v2 differences from v1 (classic):
- Offset MSB Elias gamma is written/read with inversion (`invert_mode = 1` / `inverted = true`)
- Offset LSB byte (forward mode): compressor writes `(127 - (offset-1)%128) << 1`

**Offset reconstruction in decompressor:**
```c
int msb = read_interlaced_elias_gamma(true);  // inverted for v2
// EOF check: msb == 256
last_offset = msb * 128 - (read_byte() >> 1);
```

Let's verify: if original offset = 1:
- MSB = (1-1)/128+1 = 1
- LSB byte = (127 - (1-1)%128) << 1 = 127 << 1 = 254
- Decompressor: 1*128 - (254>>1) = 128 - 127 = 1 ✓

If offset = 128:
- MSB = (128-1)/128+1 = 1 (127/128=0, +1=1)
- LSB byte = (127 - (128-1)%128) << 1 = (127-127)<<1 = 0
- Decompressor: 1*128 - (0>>1) = 128 - 0 = 128 ✓

If offset = 129:
- MSB = (129-1)/128+1 = 2
- LSB byte = (127 - (129-1)%128) << 1 = (127-0)<<1 = 254
- Decompressor: 2*128 - (254>>1) = 256 - 127 = 129 ✓

Great. So in our decompressor:
```cpp
int msb = read_elias(true);  // inverted for v2
if (msb == 256) /* EOF */;
last_offset = msb * 128 - (read_byte() >> 1);
```

And `read_elias(inverted=true)` means: `value = (value << 1) | (read_bit() ^ 1)`.

Confirming with reference:
```c
int read_interlaced_elias_gamma(int inverted) {
    int value = 1;
    while (!read_bit()) {
        value = value << 1 | read_bit() ^ inverted;
    }
    return value;
}
```

So `read_bit() ^ inverted` — when inverted=1, the data bit is XOR'd with 1. Our implementation:
```cpp
value = (value << 1) | (read_bit() ^ (inverted ? 1 : 0));
```

This matches. ✓

---

## 11. Suppressing ZX0 Output

The vendored `optimize.c` prints `[....]\n` progress. Since we vendor the source, simply delete those 3 printf/fflush sections. The patched lines:

**Remove from `optimize.c`:**
```c
// DELETE: printf("[");
// DELETE: the block:  if (index*MAX_SCALE/input_size > dots) { printf("."); fflush(stdout); dots++; }
// DELETE: printf("]\n");
```

Also remove `int dots = 2;` since it becomes unused.

---

## 12. Error Handling Summary

| Error | Behavior |
|-------|----------|
| Wrong number of CLI args | Print usage to stderr, exit 1 |
| Input file not found | Print error to stderr, exit 1 |
| Input file wrong size | Print error to stderr, exit 1 |
| Cannot create output file | Print error to stderr, exit 1 |
| ZX0 decompression fails | throw `runtime_error`, caught in main → print + exit 1 |
| Verification fails | throw `runtime_error`, caught in main → print + exit 1 |
