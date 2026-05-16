# imgviewer — Implementation Plan

## Approach

Build the viewer incrementally in testable chunks. Each milestone produces a `.k7` that can be visually verified in dcmo5. Each builds on the previous, so a regression is immediately visible.

## Prerequisite: `zx0pack` helper tool

A tiny standalone compressor needed for milestone 3+. Lives in `imgviewer/`, built from the vendored ZX0 library in `mo5z/zx0/`.

```c
// zx0pack.c — compress a file with ZX0
// Usage: zx0pack input output
```

Links against `mo5z/zx0/{compress.c,optimize.c,memory.c}`. Produces a raw ZX0 stream suitable for `includebin` in assembly. ~30 lines of C.

---

## Milestone 1: Fill Screen ✓ (already done)

`test.asm` proves bank switching and VRAM fill. Produces red screen.

---

## Milestone 2: Column→row pixel blit (reorder works)

**Goal**: Prove the column-major → row-major copy loop works correctly.

**Code**: `milestone2.asm`
1. Switch to color bank → fill with `$70` (white on black)
2. Switch to pixel bank → fill with `$00` (all black)
3. Fill scratch buffer `$7E00..$7EC7` (200 bytes = 1 column) with `$FF`
4. Run the column→row blit loop: read 200 bytes from scratch, write to pixel VRAM at stride 40
5. Halt

Only 200 bytes of $FF are in scratch (the rest is irrelevant since we only blit 1 column for the test). This validates the stride-40 inner loop and column reset logic.

**Expected result**: Single 8-pixel-wide white vertical stripe on the left edge, rest is black.

**Variant**: Set scratch start offset to test other columns (e.g., offset 200 bytes → column 1 = second stripe from left).

**Test**: Visual check in dcmo5.

---

## Milestone 3: ZX0 decompression (decompressor works)

**Goal**: Prove ZX0 decompresses correctly on the MO5.

**Code**: `milestone3.asm`
1. Switch to color bank → fill with `$70`
2. Switch to pixel bank
3. `LDX #compressed_data` / `LDU #SCRATCH`
4. `JSR zx0_decompress` → decompresses to scratch (8000 bytes)
5. Column→row blit from scratch to pixel VRAM (reuse loop from M2)
6. Halt

**Test data**: Generate at build time:
```make
# Create 8000 bytes: first 200 = $FF (col 0), rest = $00
test_pattern.bin:
	printf '%0.s\xff' {1..200} > $@ && dd if=/dev/zero bs=1 count=7800 >> $@

test_pattern.zx0: test_pattern.bin
	./zx0pack $< $@
```

Embed via `includebin "test_pattern.zx0"`.

**Expected result**: Same white stripe as milestone 2 — proves decompress + blit chain works.

**Test**: Visual check. Same output as M2 = ZX0 is correct.

---

## Milestone 4: Full viewer with real image

**Goal**: All three streams decompressed and blitted correctly.

**Rationale**: Milestones 2 + 3 prove the two hard primitives (col→row blit, ZX0 decompress). The nibble unpack is a small loop that's straightforward to get right — no need for a separate milestone. Jump straight to the full pipeline.

**Code**: `viewer.asm` — the final viewer implementing the full execution flow:
1. Switch to color bank → fill `$00`
2. Switch to pixel bank → fill `$AA`
3. Switch to color bank
4. `LDX #image_data` / `LDU #SCRATCH` → `JSR zx0_decompress` (fg, 4000 B)
5. Nibble unpack: read scratch linearly, write high nibbles to color VRAM with col→row stride
6. `LDU #SCRATCH` → `JSR zx0_decompress` (bg, 4000 B) — X already advanced past fg
7. Nibble unpack: read scratch linearly, OR low nibbles into color VRAM
8. Switch to pixel bank
9. `LDU #SCRATCH` → `JSR zx0_decompress` (pixels, 8000 B) — X already advanced past bg
10. Column→row blit from scratch to pixel VRAM
11. Halt

Image data appended via `cat viewer.bin image.mo5z`.

**Expected result**: Sharp, correct image matching `mo5z2png` output.

**Test**: Compare dcmo5 screenshot against the PNG produced by `mo5z2png`.

---

## Milestone 5: Makefile for end-to-end pipeline

**Goal**: One-command build from PNG to `.k7`.

```
make IMG=../imgs/lena.png
```

Produces `lena.k7` containing viewer + compressed lena image.

**Makefile targets**:
- `viewer.bin`: assemble viewer.asm
- `%.mo5z`: png2mo5 + mo5z
- `%.k7`: cat viewer.bin + image.mo5z → k7tool
- `test`: builds test.k7 (fill-screen sanity check)
- `milestone2`: builds milestone 2 test
- `milestone3`: builds milestone 3 test
- `zx0pack`: builds the helper compressor
- `clean`: removes all generated files

---

## File Structure (final)

```
imgviewer/
    DESIGN.md
    PLAN.md
    Makefile
    viewer.asm              ← main viewer source
    zx0_6809_standard.asm   ← ZX0 decompressor (included)
    zx0pack.c               ← standalone ZX0 compressor (build helper)
    test.asm                ← milestone 1 (fill screen)
    milestone2.asm          ← column→row blit test
    milestone3.asm          ← ZX0 decompress test
```

---

## Constants (shared across milestones)

```asm
VRAM_BASE  equ $0000
VRAM_END   equ $1F40     ; 8000 = $1F40
PIA_SYS    equ $A7C0
SCRATCH    equ $7E00
COLS       equ 40
ROWS       equ 200
ROW_PAIRS  equ 100

ZX0_VAR1   equ $80       ; DP offset (physical $2080)
ZX0_VAR2   equ $82       ; DP offset (physical $2082)
ZX0_VAR3   equ $84       ; DP offset (physical $2084)
```

---

## Risk Mitigation

| Risk | Mitigation |
|------|-----------|
| ZX0 decompressor bug | Milestone 3 isolates it — if decompression fails, the screen is obviously wrong |
| Column→row indexing error | Milestone 2 isolates it — a stripe in the wrong place is instantly visible |
| Nibble shift/OR wrong | Milestone 4 tests the full image — corruption is visually obvious |
| Binary too large | `mo5z` prints compressed sizes; Makefile checks payload fits below `$7E00` |
| DP conflicts with monitor | Variables at `$80–$85` are in unused area of `$2000–$20FF` page |
