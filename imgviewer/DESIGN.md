# imgviewer — MO5 Image Viewer

Display a single ZX0-compressed MO5 image from cassette tape.

6809 assembly, targeting lwasm (lwtools). 309 bytes of viewer code.

## Overview

A single `.k7` tape file contains the viewer code (pre-built `mo5zviewer.bin`), ZX0 decompressor, and one compressed image concatenated into a flat binary. Boot, decompress, display, halt.

The viewer binary is a static artifact — assemble it once, then reuse for any image by concatenating with a `.mo5z` file.

## Tape Structure

One tape entry:

```
[viewer + ZX0 decompressor][fg_zx0][bg_zx0][pixels_zx0]
```

- Load address: `$2800`
- Exec address: `$2800`
- Built with `k7tool`

## mo5z Stream Order

The `.mo5z` format stores streams as:

```
[fg_zx0][bg_zx0][pixels_zx0]
```

Color-first ordering enables progressive visual reveal during decompression.

## Execution Flow

1. **Fill color VRAM** ← `$00` (black on black) — screen goes black
2. **Fill pixel VRAM** ← `$AA` (alternating bits) — still black, invisible
3. **Decompress fg** → scratch buffer (4000 bytes)
4. **Unpack fg nibbles** — column→row reorder, write `nibble << 4` to color VRAM
5. **Decompress bg** → scratch buffer (4000 bytes, reused)
6. **Unpack bg nibbles** — column→row reorder, OR `nibble` into color VRAM
7. **Decompress pixels** → scratch buffer (8000 bytes)
8. **Copy pixels** — column→row reorder to pixel VRAM
9. **Halt** — `BRA *` (image stays on screen forever)

### Visual Effect

The progressive reveal:
- Screen blacks out
- Foreground colors appear in a dithered pattern (step 4)
- Background fills in — low-resolution "blocky" image visible (step 6)
- Pixel data snaps the image into full sharpness (step 8)

## Memory Layout

```
$0000–$1F3F  VRAM (pixel or color, bank-switched via $A7C0 bit 0)
$2800        [viewer code + ZX0 decompressor]  309 bytes
$2935+       [fg_zx0][bg_zx0][pixels_zx0]     ≤ ~16 KB (worst case)
$7E00–$9DFF  Scratch buffer (8 KB, fixed address)
$9E00–$9FFF  Stack (grows down)
```

Worst-case total: 309 + 16,000 = ~16.3 KB for the binary. Scratch is 8 KB at a fixed high address. Fits comfortably in the 30 KB user RAM (`$2800–$9FFF`).

## Bank Switching

Both pixel and color VRAM occupy `$0000–$1F3F`. Bit 0 of PIA register `$A7C0` selects the active bank:
- Bit 0 = 0 → color bank (COULEUR)
- Bit 0 = 1 → pixel bank (FORME)

Execution order grouped by bank to minimize switches:

1. **Color mode** (bit 0 = 0): fill `$00`
2. **Pixel mode** (bit 0 = 1): fill `$AA`
3. **Color mode** (bit 0 = 0): decompress & unpack fg, decompress & unpack bg
4. **Pixel mode** (bit 0 = 1): decompress & copy pixels

Three bank switches total. The screen goes black at step 1 and stays black through step 2 (black-on-black regardless of pixel values). Colors appear during step 3, pixels sharpen during step 4.

## Nibble Unpack + Column→Row Blit

### Color passes (fg, bg)

Source: scratch buffer, read linearly (`LDA ,X+`)
Destination: color VRAM, stride-40 writes

```
for col = 0..39:
    dest = VRAM + col
    for row_pair = 0..99:
        byte = *src++
        *dest |= (byte >> 4) << shift    ; shift=4 for fg, 0 for bg
        dest += 40
        *dest |= (byte & $0F) << shift
        dest += 40
    ; reset dest to next column
```

### Pixel pass

Source: scratch buffer, read linearly
Destination: pixel VRAM, stride-40 writes

```
for col = 0..39:
    dest = VRAM + col
    for row = 0..199:
        *dest = *src++
        dest += 40
    ; reset dest to next column
```

## ZX0 Decompressor

Uses `imgviewer/zx0_6809_standard.asm` — a ZX0 **v1** decompressor (invert_mode=0) with all DP optimizations enabled.

DP register is `$20` on MO5 (direct page = `$2000–$20FF`). Variables placed in unused area:

```
ZX0_VAR1 equ $80    ; 1 byte  — bit stream   (physical $2080)
ZX0_VAR2 equ $82    ; 2 bytes — offset        (physical $2082)
ZX0_VAR3 equ $84    ; 2 bytes — temp save     (physical $2084)
```

`ZX0_ONE_TIME_USE` is **not** set — the decompressor is called 3 times and must reinitialize each time.

## Assembler

lwasm from [lwtools](https://www.lwtools.ca/) (v4.24+). Only needed to rebuild `mo5zviewer.bin`:

```
brew install lwtools
lwasm --raw -o mo5zviewer.bin viewer.asm
```

## Image Data Location

The viewer finds the compressed image via a label at the end of its own code:

```asm
        org $2800
        ; ... viewer code + ZX0 decompressor (309 bytes) ...
image_data:
        ; .mo5z bytes concatenated here at build time
```

The viewer loads the address with `LDX #image_data` — a compile-time constant. After `cat mo5zviewer.bin image.mo5z`, `image_data` = `$2800 + 309` = `$2935`.

## Build Pipeline

### End-user workflow (no assembler needed)

```
png2mo5 input.png --bin → pixels.bin + colors.bin
mo5z pixels.bin colors.bin → image.mo5z
cat mo5zviewer.bin image.mo5z → payload.bin
k7tool -o output.k7 payload.bin:0x2800:0x2800
```

`mo5zviewer.bin` is a pre-built 309-byte static artifact checked into the repo.

### Developer workflow (rebuilding the viewer)

Requires lwasm from [lwtools](https://www.lwtools.ca/) (v4.24+):

```
brew install lwtools
lwasm --raw -o mo5zviewer.bin viewer.asm
```

Orchestrated by `Makefile` in `imgviewer/`.

## Testing

Visual verification in dcmo5 emulator. Load the `.k7`, confirm the image displays correctly.

The `mo5z` tool already performs round-trip verification (decompress + reverse transform + compare) at compression time, so correctness of the compressed data is guaranteed before it reaches the viewer.
