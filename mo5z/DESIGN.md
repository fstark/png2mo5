# mo5z — MO5 Image Compressor

Standalone tool that compresses MO5 pixel + color banks into a single ZX0-compressed file for fast loading on real hardware.

## Usage

```
mo5z <pixels.bin> <colors.bin> [-o <stem>]
```

- `pixels.bin`: 8000-byte pixel bank (row-major, as output by png2mo5)
- `colors.bin`: 8000-byte color bank (row-major, `fg<<4 | bg` per block)
- `-o <stem>`: output stem (default: `out`)

Produces `<stem>.mo5z`.

## Output Format

Three concatenated ZX0 streams (self-terminating, no header):

```
[pixels_zx0][fg_nibbles_zx0][bg_nibbles_zx0]
```

The decompressor reads sequentially: decompress stream 1, pointer advances, decompress stream 2, pointer advances, decompress stream 3.

## Pipeline

```
read bins
    → canonicalize (per-block nibble sorting + free-nibble optimization)
    → reorder to column-major
    → split color nibbles + pack pairs
    → compress each stream with ZX0
    → verify round-trip
    → concatenate → write .mo5z
```

## Canonicalization

Each 8-pixel block has two equivalent representations:
- `(pixels, n1<<4 | n2)`
- `(pixels XOR 0xFF, n2<<4 | n1)`

### Swap Decision (sorted)

For each non-solid block, the smaller nibble always goes to stream 1, the larger to stream 2. If the original `n_a < n_b`, keep as-is; otherwise swap (invert pixels, flip nibbles). This produces highly regular nibble streams that compress well with ZX0.

When `n_a == n_b`, the block is treated as a free-nibble case (same as solid blocks).

### Free Nibble (solid blocks)

When `pixels = 0x00` or `pixels = 0xFF`, only one nibble is visible. The invisible nibble is a free variable set to maximize run continuity:

1. If the visible color matches `prev_n1` only → place it in stream 1 (pixels = `0xFF`), set stream 2 to `prev_n2`.
2. All other cases → place it in stream 2 (pixels = `0x00`), set stream 1 to `prev_n1`.

## Data Layout

All three streams use **column-major** ordering: column 0 rows 0–199, column 1 rows 0–199, ..., column 39 rows 0–199.

- **Pixel stream**: 8000 bytes, column-major.
- **Nibble streams**: each 4000 nibbles, packed as vertically-adjacent pairs: `entry[0]<<4 | entry[1]`, giving 100 packed bytes per column, 4000 bytes per stream.

## Compression

ZX0 optimal compression (Einar Saukas). Source vendored from `github.com/einar-saukas/ZX0`. Linked directly into the binary — no subprocess, no temp files.

## Verification

After compression, all 3 streams are decompressed in-process and the full reverse transform is applied (unpack nibbles, recombine color bytes, column-to-row reorder). The result is compared block-by-block for visual equivalence against the original inputs. Mismatch = hard error.

## Stats

Always printed to stdout:

```
pixels: 8000 → 3412 (42.6%)
fg:     4000 →  891 (22.3%)
bg:     4000 →  743 (18.6%)
total: 16000 → 5046 (31.5%)
wrote out.mo5z
```

## Build

- Language: C++
- Tests: Catch2
- Location: `mo5z/` (sibling to `png2mo5/`)
- ZX0 source: vendored in `mo5z/zx0/`

## Decompressor

Not included. The 6809 MO5 loader/decompressor is a separate concern. A known 6809 ZX0 decompressor exists at `github.com/dougmasten/zx0-6x09`.
