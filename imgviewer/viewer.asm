; viewer.asm — MO5 Image Viewer
; Decompresses a .mo5z image (fg/bg/pixels) and displays it.
; Image data is appended after this code at build time.
;
; Stream order: [fg_zx0][bg_zx0][pixels_zx0]

        org  $2800

VRAM_BASE equ $0000
VRAM_END  equ $1F40     ; 8000
PIA_SYS   equ $A7C0
SCRATCH   equ $7E00
COLS      equ 40
ROWS      equ 200
ROW_PAIRS equ 100

ZX0_VAR1  equ $80
ZX0_VAR2  equ $82
ZX0_VAR3  equ $84

; ============================================================================
; Entry point
; ============================================================================
start:
        ; === Phase 1: Switch to color bank (bit 0 = 0), fill $00 (screen goes black) ===
        lda  PIA_SYS
        anda #$FE
        sta  PIA_SYS

        ldx  #VRAM_BASE
        clra
fill_color:
        sta  ,x+
        cmpx #VRAM_END
        bne  fill_color

        ; === Phase 2: Switch to pixel bank (bit 0 = 1), fill $AA ===
        lda  PIA_SYS
        ora  #$01
        sta  PIA_SYS

        ldx  #VRAM_BASE
        lda  #$AA
fill_pix:
        sta  ,x+
        cmpx #VRAM_END
        bne  fill_pix

        ; === Phase 3: Switch to color bank (bit 0 = 0) for fg+bg unpack ===
        lda  PIA_SYS
        anda #$FE
        sta  PIA_SYS

        ; --- Decompress fg stream (4000 bytes) to scratch ---
        ldx  #image_data
        ldu  #SCRATCH
        jsr  zx0_decompress
        ; X now points past fg_zx0 stream (start of bg_zx0)
        stx  next_stream    ; save for bg decompression

        ; --- Unpack fg nibbles: high nibble << 4 → color VRAM ---
        ; Source: scratch, 4000 bytes (100 packed pairs per column, 40 columns)
        ; Dest: color VRAM, column→row, write high nibble to high nibble of color byte
        ldy  #SCRATCH       ; src pointer (linear read)
        ldu  #VRAM_BASE     ; dest base
        ldb  #COLS
        stb  col_count

fg_col_loop:
        ldb  #ROW_PAIRS
fg_pair_loop:
        lda  ,y+            ; packed byte: fg_hi|fg_lo
        ; High nibble → even row
        anda #$F0           ; isolate high nibble (already shifted)
        sta  ,u             ; write to color VRAM
        ; Low nibble → odd row
        lda  -1,y           ; re-read the packed byte
        lsla
        lsla
        lsla
        lsla                ; shift low nibble to high position
        sta  40,u           ; write to next row
        leau 80,u           ; advance 2 rows (2 * 40)
        decb
        bne  fg_pair_loop
        ; Reset dest to next column: back up 200 rows, advance 1 byte
        ; current U = base + col + 200*40, need base + col + 1
        ; offset = -(200*40) + 1 = -7999
        leau -7999,u
        dec  col_count
        bne  fg_col_loop

        ; --- Decompress bg stream (4000 bytes) to scratch ---
        ldx  next_stream
        ldu  #SCRATCH
        jsr  zx0_decompress
        ; X now points past bg_zx0 (start of pixels_zx0)
        stx  next_stream

        ; --- Unpack bg nibbles: low nibble OR'd into color VRAM ---
        ldy  #SCRATCH
        ldu  #VRAM_BASE
        ldb  #COLS
        stb  col_count

bg_col_loop:
        ldb  #ROW_PAIRS
bg_pair_loop:
        lda  ,y+            ; packed byte: bg_hi|bg_lo
        ; High nibble → even row (shift right 4 to get low nibble value)
        lsra
        lsra
        lsra
        lsra
        ora  ,u             ; OR into existing color byte
        sta  ,u
        ; Low nibble → odd row
        lda  -1,y           ; re-read packed byte
        anda #$0F           ; isolate low nibble
        ora  40,u           ; OR into existing color byte at next row
        sta  40,u
        leau 80,u           ; advance 2 rows
        decb
        bne  bg_pair_loop
        leau -7999,u        ; next column
        dec  col_count
        bne  bg_col_loop

        ; === Phase 4: Switch to pixel bank (bit 0 = 1), decompress & blit pixels ===
        lda  PIA_SYS
        ora  #$01
        sta  PIA_SYS

        ; --- Decompress pixels stream (8000 bytes) to scratch ---
        ldx  next_stream
        ldu  #SCRATCH
        jsr  zx0_decompress

        ; --- Column→row blit: scratch → pixel VRAM ---
        ldy  #SCRATCH
        ldu  #VRAM_BASE
        ldb  #COLS
        stb  col_count

pix_col_loop:
        ldb  #ROWS
pix_row_loop:
        lda  ,y+
        sta  ,u
        leau 40,u
        decb
        bne  pix_row_loop
        leau -7999,u        ; next column
        dec  col_count
        bne  pix_col_loop

        ; === Done — halt forever ===
halt:
        bra  halt

; ============================================================================
; Variables (in code segment — small, acceptable)
; ============================================================================
col_count:   fcb 0
next_stream: fdb 0

; ============================================================================
; ZX0 decompressor
; ============================================================================
        include zx0_6809_standard.asm

; ============================================================================
; Image data starts here (appended at build time via cat)
; ============================================================================
image_data:

        end  start
