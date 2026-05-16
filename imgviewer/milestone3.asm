; milestone3.asm — ZX0 decompression test
; Decompresses a ZX0-compressed column-major pattern to scratch,
; then blits col→row to pixel VRAM.
; Expected: same white stripe as milestone 2 (proves ZX0 works).

        org  $2800

VRAM_BASE equ $0000
VRAM_END  equ $1F40
PIA_SYS   equ $A7C0
SCRATCH   equ $7E00
COLS      equ 40
ROWS      equ 200

ZX0_VAR1  equ $80
ZX0_VAR2  equ $82
ZX0_VAR3  equ $84

start:
        ; --- Switch to color bank (bit 0 = 0) ---
        lda  PIA_SYS
        anda #$FE
        sta  PIA_SYS

        ldx  #VRAM_BASE
        lda  #$70
fill_color:
        sta  ,x+
        cmpx #VRAM_END
        bne  fill_color

        ; --- Switch to pixel bank (bit 0 = 1) ---
        lda  PIA_SYS
        ora  #$01
        sta  PIA_SYS

        ldx  #VRAM_BASE
        clra
fill_pix:
        sta  ,x+
        cmpx #VRAM_END
        bne  fill_pix

        ; --- Decompress test pattern to scratch ---
        ldx  #compressed_data
        ldu  #SCRATCH
        jsr  zx0_decompress

        ; --- Column→row blit: all 40 columns ---
        ldy  #SCRATCH       ; src (read linearly)
        ldb  #COLS          ; column counter
        ldx  #VRAM_BASE     ; base dest (increments by 1 per column)

col_loop:
        pshs b,x           ; save col counter and base dest
        tfr  x,u           ; U = dest pointer for this column
        ldb  #ROWS          ; 200 rows
row_loop:
        lda  ,y+            ; read from scratch
        sta  ,u             ; write to VRAM
        leau 40,u           ; next row
        decb
        bne  row_loop

        puls b,x           ; restore col counter and base
        leax 1,x           ; next column
        decb
        bne  col_loop

        ; --- Halt ---
halt:
        bra  halt

; --- ZX0 decompressor ---
        include zx0_6809_standard.asm

; --- Compressed test data (generated at build time) ---
compressed_data:
        includebin "test_pattern.zx0"

        end  start
