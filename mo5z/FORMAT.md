# .mo5z File Format

## Overview

A `.mo5z` file contains a single MO5 320×200 image compressed into three concatenated ZX0 v1 streams. No header, no metadata — just raw compressed data back-to-back.

## File Layout

```
[fg_nibbles_zx0][bg_nibbles_zx0][pixels_zx0]
```

Each ZX0 stream is self-terminating (the decoder knows when it's done), so the next stream starts at the byte immediately following the previous one.

Color-first ordering allows a viewer to progressively reveal the image: colors appear before pixel data.

## Decompressed Streams

| Stream | Decompressed size | Contents |
|--------|-------------------|----------|
| fg | 4000 bytes | Foreground nibble pairs, packed |
| bg | 4000 bytes | Background nibble pairs, packed |
| pixels | 8000 bytes | Pixel data, column-major |

## Decoding Steps

To reconstruct the standard MO5 pixel bank (8000 bytes) and color bank (8000 bytes, `fg<<4 | bg`):

### 1. Decompress

Decompress the three ZX0 v1 streams sequentially:
- Stream 1 → `fg_packed[4000]`
- Stream 2 → `bg_packed[4000]`
- Stream 3 → `pixels[8000]` (column-major)

### 2. Unpack Nibble Pairs

Each packed byte holds two vertically-adjacent nibbles:

```
fg_nibbles[2*i]   = fg_packed[i] >> 4
fg_nibbles[2*i+1] = fg_packed[i] & 0x0F
```

Same for `bg_packed` → `bg_nibbles`. Result: two arrays of 8000 nibbles each.

### 3. Recombine Color Bytes

For each block `i` (in column-major order):

```
color[i] = (fg_nibbles[i] << 4) | bg_nibbles[i]
```

Result: `color[8000]` in column-major order.

### 4. Reorder Column-Major → Row-Major

All three arrays (pixels, color) are stored column-major:
- Index `col * 200 + row` in column-major = index `row * 40 + col` in row-major.

```
for col = 0..39:
    for row = 0..199:
        pixel_bank[row * 40 + col] = pixels[col * 200 + row]
        color_bank[row * 40 + col] = color[col * 200 + row]
```

Result: standard MO5 pixel bank and color bank, ready to blit to VRAM.

## Column-Major Ordering

Blocks are stored column-by-column, top-to-bottom:

```
col 0: rows 0, 1, 2, ..., 199
col 1: rows 0, 1, 2, ..., 199
...
col 39: rows 0, 1, 2, ..., 199
```

This groups vertically adjacent blocks together, which improves ZX0 compression since MO5 images tend to have vertical continuity.

## ZX0 v2

The streams use ZX0 with inverted Elias encoding (v2 format). A 6809 decompressor is available at `github.com/dougmasten/zx0-6x09`.

## MO5 VRAM Layout

For reference, the MO5 video RAM layout that the decoded data targets:

- **Pixel bank** at `$0000–$1F3F`: 8000 bytes, each byte = 8 pixels in a block (MSB = leftmost pixel). Row-major: row 0 cols 0–39, row 1 cols 0–39, etc.
- **Color bank** at `$2000–$3F3F`: 8000 bytes, each byte = `fg<<4 | bg` for one 8-pixel block. Same row-major layout.
