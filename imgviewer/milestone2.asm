; milestone2.asm — Column→row blit test
; Fills one column of scratch with $FF, blits to pixel VRAM using col→row loop.
; Expected: single white vertical stripe on left edge.

        org  $2800

VRAM_BASE equ $0000
VRAM_END  equ $1F40     ; 8000
PIA_SYS   equ $A7C0
SCRATCH   equ $7E00
COLS      equ 40
ROWS      equ 200

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

        ; --- Fill scratch: first 200 bytes = $FF (column 0), rest = $00 ---
        ldx  #SCRATCH
        lda  #$FF
        ldb  #200
fill_col0:
        sta  ,x+
        decb
        bne  fill_col0

        ; --- Column→row blit: 1 column test ---
        ; Source: scratch (200 bytes linear for col 0)
        ; Dest: pixel VRAM col 0, stride 40
        ldx  #SCRATCH       ; src
        ldu  #VRAM_BASE     ; dest = VRAM + col (col=0)
        ldb  #200           ; row counter
blit_loop:
        lda  ,x+            ; read from scratch
        sta  ,u             ; write to VRAM
        leau 40,u           ; next row (stride = 40 bytes)
        decb
        bne  blit_loop

        ; --- Halt ---
halt:
        bra  halt

        end  start
