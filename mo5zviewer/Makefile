K7TOOL   := ../k7tool/k7tool
PNG2MO5  := ../png2mo5/png2mo5
MO5Z     := ../mo5z/mo5z

# Default image for the viewer
IMG      ?= ../imgs/lena.png

# Derive name from image
IMG_STEM  = $(basename $(notdir $(IMG)))

.PHONY: all viewer clean

all: mo5zviewer.bin

ZX0_ASM  := ../lib/zx0/asm/zx0_6809_standard.asm

# === Pre-built viewer stub (assemble once, reuse for any image) ===
mo5zviewer.bin: viewer.asm $(ZX0_ASM)
	lwasm --raw --includedir=../lib/zx0/asm -o $@ $<

# === Full K7 from a PNG (uses pre-built viewer) ===
viewer: $(IMG_STEM).k7

$(IMG_STEM).k7: $(IMG_STEM).mo5z mo5zviewer.bin
	cat mo5zviewer.bin $< > payload.bin
	$(K7TOOL) -o $@ payload.bin:0x2800:0x2800
	rm -f payload.bin

$(IMG_STEM).mo5z: $(IMG_STEM)_pixels.bin $(IMG_STEM)_colors.bin
	$(MO5Z) $^ -o $(IMG_STEM)

$(IMG_STEM)_pixels.bin $(IMG_STEM)_colors.bin: $(IMG)
	$(PNG2MO5) $< --bin -o $(IMG_STEM)

# === Clean ===
clean:
	rm -f *.bin *.k7 *.mo5z payload.bin

