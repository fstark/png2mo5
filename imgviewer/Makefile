K7TOOL   := ../k7tool/k7tool
PNG2MO5  := ../png2mo5/png2mo5
MO5Z     := ../mo5z/mo5z
ZX0_SRC  := ../mo5z/zx0

# Default image for the viewer
IMG      ?= ../imgs/lena.png

# Derive name from image
IMG_STEM  = $(basename $(notdir $(IMG)))

.PHONY: all test milestone2 milestone3 viewer clean

all: viewer

# === Milestone 1: fill screen test ===
test: test.k7

test.k7: test.bin
	$(K7TOOL) -o $@ $<:0x2800:0x2800

test.bin: test.asm
	lwasm --raw -o $@ $<

# === Milestone 2: column→row blit ===
milestone2: milestone2.k7

milestone2.k7: milestone2.bin
	$(K7TOOL) -o $@ $<:0x2800:0x2800

milestone2.bin: milestone2.asm
	lwasm --raw -o $@ $<

# === Milestone 3: ZX0 decompression ===
milestone3: milestone3.k7

milestone3.k7: milestone3.bin
	$(K7TOOL) -o $@ $<:0x2800:0x2800

milestone3.bin: milestone3.asm zx0_6809_standard.asm test_pattern.zx0
	lwasm --raw -o $@ $<

# Test pattern: column 0 = $FF (200 bytes), rest = $00 (7800 bytes)
test_pattern.bin:
	python3 -c "import sys; sys.stdout.buffer.write(b'\xff'*200 + b'\x00'*7800)" > $@

test_pattern.zx0: test_pattern.bin zx0pack
	./zx0pack $< $@

# === Full viewer ===
viewer: $(IMG_STEM).k7

$(IMG_STEM).k7: payload.bin
	$(K7TOOL) -o $@ $<:0x2800:0x2800

payload.bin: viewer.bin $(IMG_STEM).mo5z
	cat $^ > $@

viewer.bin: viewer.asm zx0_6809_standard.asm
	lwasm --raw -o $@ $<

$(IMG_STEM).mo5z: $(IMG_STEM)_pixels.bin $(IMG_STEM)_colors.bin
	$(MO5Z) $^ -o $(IMG_STEM)

$(IMG_STEM)_pixels.bin $(IMG_STEM)_colors.bin: $(IMG)
	$(PNG2MO5) $< --bin -o $(IMG_STEM)

# === zx0pack helper ===
zx0pack: zx0pack.c $(ZX0_SRC)/compress.c $(ZX0_SRC)/optimize.c $(ZX0_SRC)/memory.c
	cc -O2 -o $@ $^

# === Clean ===
clean:
	rm -f *.bin *.k7 *.mo5z *.zx0 zx0pack payload.bin test_pattern.bin

