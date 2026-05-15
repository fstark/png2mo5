# png2mo5 — Design Document

Convert any image to Thomson MO5 video format.

C++ single-file implementation. Zero external dependencies beyond stb headers.

## MO5 Video System

The Thomson MO5 (1984) displays a **320×200** pixel screen using a fixed
**16-color palette**. Video memory is 16 KB split into two 8 KB banks:

| Bank     | Size  | Content                              |
|----------|-------|--------------------------------------|
| pixels   | 8000 B | Pixel bitmap — 1 bit per pixel       |
| colors   | 8000 B | Color attributes — 1 byte per 8-pixel block |

(Thomson's documentation calls these "forme" and "couleur" respectively.)

Both banks are 40 bytes wide × 200 rows = 8000 bytes.

### Pixel encoding (pixels)

Each byte encodes 8 horizontal pixels. **Bit 7 (MSB) is the leftmost pixel.**

```
Bit:    7   6   5   4   3   2   1   0
Pixel:  ←  ···························  →
```

- Bit = 1 → foreground color (from colors high nibble)
- Bit = 0 → background color (from colors low nibble)

### Color encoding (colors)

One byte per 8-pixel block. Two colors per block (the MO5 constraint).

```
Bit:    7   6   5   4   3   2   1   0
        ├── foreground ────┤── background ──┤
```

- Bits 7–4: foreground color index (0–15) — applied where pixels bit = 1
- Bits 3–0: background color index (0–15) — applied where pixels bit = 0

The two colors can be **any** two of the 16 palette entries (including the same
color twice). There is no requirement for one of them to be black.

### The 2-color constraint

This is the fundamental MO5 limitation: **each group of 8 horizontal pixels
can use at most 2 colors.** There are no per-pixel color choices — only per-block.
Vertical neighbors are independent (attribute blocks are 8×1, not 8×8).

This means:
- Adjacent 8-pixel blocks can have completely different color pairs
- The constraint is purely horizontal, per-row
- A good converter must choose the best 2 colors per block

## MO5 Palette

The MO5 has a **fixed** 16-color palette. Colors 0–7 are the saturated primaries;
colors 8–15 are pastel/intermediate variants.

| Index | Name         | R   | G   | B   | Hex       |
|-------|-------------|-----|-----|-----|-----------|
| 0     | Black        |   0 |   0 |   0 | `#000000` |
| 1     | Red          | 255 |   0 |   0 | `#FF0000` |
| 2     | Green        |   0 | 255 |   0 | `#00FF00` |
| 3     | Yellow       | 255 | 255 |   0 | `#FFFF00` |
| 4     | Blue         |   0 |   0 | 255 | `#0000FF` |
| 5     | Magenta      | 255 |   0 | 255 | `#FF00FF` |
| 6     | Cyan         |   0 | 255 | 255 | `#00FFFF` |
| 7     | White        | 255 | 255 | 255 | `#FFFFFF` |
| 8     | Gray         | 128 | 128 | 128 | `#808080` |
| 9     | Pink         | 255 | 128 | 128 | `#FF8080` |
| 10    | Light Green  | 128 | 255 | 128 | `#80FF80` |
| 11    | Light Yellow | 255 | 255 | 128 | `#FFFF80` |
| 12    | Light Blue   | 128 | 128 | 255 | `#8080FF` |
| 13    | Purple       | 255 | 128 | 255 | `#FF80FF` |
| 14    | Light Cyan   | 128 | 255 | 255 | `#80FFFF` |
| 15    | Orange       | 255 | 128 |   0 | `#FF8000` |

## Output format

The converter produces:

### MO5 binary files

**`<name>_pixels.bin`** — 8000 bytes. Raw pixel bitmap. Byte 0 = top-left
8 pixels, byte 39 = top-right 8 pixels, byte 40 = second row left, etc.

```
Offset = y * 40 + (x / 8)
```

**`<name>_colors.bin`** — 8000 bytes. Raw color attributes. Same layout as
pixels — one byte per 8-pixel block, same offset formula. Each byte =
`(fg << 4) | bg`.

### PNG preview

**`<name>_preview.png`** — a 320×200 PNG rendered from the MO5
representation. Shows exactly what the image would look like on an MO5
(2 colors per 8-pixel block, palette-quantized), viewable on any modern system.

### Relationship

For any offset `i` (0 ≤ i < 8000):

```
block_x = (i % 40) * 8      # pixel column of leftmost pixel in block
block_y = i / 40             # pixel row

fg = color[i] >> 4           # foreground color index
bg = color[i] & 0x0F         # background color index

for bit in 7..0:
    pixel_color = fg if (form[i] >> bit) & 1 else bg
```

## Conversion Algorithm

### Pipeline overview

```
Input image (any format stb supports)
    │
    ▼
[1] Load & scale to 320×200 (Lanczos, crop if aspect differs)
    │
    ▼
[2] Convert all pixels from sRGB → linear RGB → XYZ → CIELAB
    │
    ▼
[3] Interleaved color selection + Floyd-Steinberg dithering
    (serpentine scan order, block-by-block)
    │
    ▼
[4] Emit MO5 binary (pixels.bin + colors.bin) + PNG preview
```

Steps 1–2 are preprocessing. Step 3 is the core algorithm. Step 4 is output.

### Step 1 — Input loading and scaling

Use `stb_image.h` to load any supported format (PNG, JPG, BMP, TGA, etc.).
Use `stb_image_resize2.h` to scale to 320×200.

- **Default resampling:** Lanczos (best quality for downscaling photos).
- **`--nearest` flag:** Nearest-neighbor resampling (for pixel art).
- **Aspect ratio mismatch:** Scale to fill 320×200 completely (preserving
  aspect ratio), crop overflow from center.

### Step 2 — Color space conversion

Convert every pixel from sRGB to CIELAB:

```
sRGB → linear RGB (gamma decode) → CIE XYZ (D65) → CIELAB
```

sRGB gamma decode (per channel, normalized 0–1):

```
if (srgb <= 0.04045)
    linear = srgb / 12.92
else
    linear = pow((srgb + 0.055) / 1.055, 2.4)
```

The working buffer is a 320×200 array of `float[3]` (L*, a*, b*).

Pre-convert the 16-entry MO5 palette to Lab once at startup.

### Step 3 — Interleaved color selection + dithering

This is the core of the converter. Color pair selection and error diffusion
are interleaved — not run as separate passes.

#### Scanline processing order (serpentine)

Rows alternate direction to prevent directional dithering bias:

- Even rows: left-to-right (block 0 → 39, pixel 0 → 7)
- Odd rows: right-to-left (block 39 → 0, pixel 7 → 0)

#### Architecture: decoupled select_pair / dither_and_commit

Each block is processed in two phases:

```
pair = select_pair(pixels)          // choose best color pair
dither_and_commit(pixels, pair)     // FS with locked pair, diffuses error × damping
```

This enables:
- `--no-dither` mode: call `select_pair` + static assign, skip `dither_and_commit`
- Swapping selection strategies without touching the dither code
- Independent unit testing of each function

#### select_pair — choose the best color pair

For each 8-pixel block, read the 8 Lab pixel values from the working buffer.
These values already include accumulated error from previously processed blocks
(above and to the side). The error from neighboring blocks has already "leaked"
into these pixels, so the pair selection naturally adapts.

Brute-force all 136 possible color pairs (C(16,2) + 16 same-color pairs).
For each candidate pair, **simulate full 8-pixel Floyd-Steinberg dithering**
within the block. Pick the pair with minimum **total outgoing error**.

```
best_pair = null
best_outgoing = +∞

for each (c1, c2) in all 136 palette pairs:
    scratch[0..7] = input_pixels[0..7]      // copy
    outgoing_error = 0

    for i = 0..7 (in current scan direction):
        chosen = closer_of(scratch[i], c1, c2)   // CIE76 distance
        error = scratch[i] - palette_lab[chosen]  // 3-component Lab vector

        // Diffuse error within the block only (rightward)
        if i is not the last pixel:
            scratch[i+1] += error * 7/16

        // Everything else is "outgoing" — it leaves the block
        // (downward: 3/16 + 5/16 + 1/16 = 9/16, plus any rightward
        //  that falls past pixel 7)
        outgoing_error += magnitude(error) * 9/16
        if i == last pixel:
            outgoing_error += magnitude(error) * 7/16  // rightward has nowhere to go

    if outgoing_error < best_outgoing:
        best_outgoing = outgoing_error
        best_pair = (c1, c2)

return best_pair
```

Notes:
- `magnitude(error)` = squared Euclidean in Lab (L²+a²+b²). No sqrt needed
  since we only compare.
- The intra-block rightward diffusion (7/16) is applied during simulation so
  later pixels see realistic adjusted values — same as actual FS would.
- Only rightward (7/16) is applied within the block. The other three FS
  directions (↙3/16, ↓5/16, ↘1/16) all go to the next row = outgoing.
- On RTL rows, "rightward" means pixel i-1 and the kernel is mirrored.

The pair that traps the most error internally leaves the least mess for
downstream blocks, directly optimizing global image quality.

**Cost:** 136 × 8-pixel FS simulations × 8000 blocks. ~100ms. Acceptable.

#### dither_and_commit — diffuse error from locked assignments

The pixel assignments (bitmap) are locked from `select_pair`'s trial FS.
Process pixels one by one (in the current scan direction). For each pixel:

1. Read the current Lab value from the working buffer (includes error).
2. Look up the already-chosen color from the bitmap (fg or bg).
3. Compute the quantization error: `error = current_lab - chosen_palette_lab`.
4. Diffuse the error using Floyd-Steinberg weights (mirrored on RTL rows),
   scaled by a **damping factor of ~0.9**:

```
LTR direction:            RTL direction:
        *    7/16         7/16    *
  3/16  5/16  1/16        1/16  5/16  3/16
```

5. Clamp the destination pixel values to valid Lab ranges:
   L* ∈ [0, 100], a* ∈ [-128, 127], b* ∈ [-128, 127].

The pixel's fg/bg assignment sets the corresponding bit in the form byte.

#### Error damping

Outgoing error is scaled by ~90% before diffusion. The lost 10% decays
exponentially — long-range pollution dies before it causes visible artifacts.
This prevents "orphan error" from constrained blocks (where neither palette
color is close) from propagating indefinitely and corrupting distant regions.

The exact damping factor (~0.9) is an empirical tuning parameter.

### Step 4 — Output

- **Binary:** Write `pixels.bin` (8000 bytes) and `colors.bin` (8000 bytes)
  directly from the form/color arrays.
- **Preview PNG:** Render the MO5 representation back to a 320×200 RGB image
  using the palette, write via `stb_image_write.h`.

## CLI Interface

```
png2mo5 input.png [-o output] [--bin] [--preview] [--nearest] [--no-dither] [--damping F]
```

- `input.png` — Any image format stb supports.
- `-o output` — Output path. In preview mode (default), this is the literal
  filename (e.g. `-o out.png`). In `--bin` mode, this is a basename
  (e.g. `-o out` → `out_pixels.bin` + `out_colors.bin`).
  Defaults to input filename stem when omitted.

Output modes:
- Default (no flags) — Preview only: `<basename>_preview.png`.
- `--bin` — Binary output only: `<basename>_pixels.bin` + `<basename>_colors.bin`.
- `--bin --preview` — Both binaries and preview.

Flags:
- `--nearest` — Nearest-neighbor resampling (for pixel art).
- `--no-dither` — Skip error diffusion, straight quantization only.
- `--damping F` — Error damping factor, 0.0–1.0 (default: 0.9).

## Build

```
make
```

Single `.cpp` file + stb headers in `stb/`. Trivial Makefile:

```makefile
png2mo5: png2mo5.cpp
	$(CXX) -O2 -o $@ $<
```

## Dependencies

All vendored, zero install steps:
- `stb_image.h` — image loading
- `stb_image_resize2.h` — image scaling
- `stb_image_write.h` — PNG output
