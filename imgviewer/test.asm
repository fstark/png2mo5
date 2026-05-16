; test.asm — Minimal screen test for MO5
; Fills pixel VRAM with $FF and color VRAM with $17 (red on white).
; Proves the build pipeline: assemble → k7tool → load in emulator.

        org  $2800

VRAM_BASE equ $0000
VRAM_END  equ $1F40
PIA_SYS   equ $A7C0

start:
        ; --- Switch to pixel bank (bit 0 = 1) ---
        lda  PIA_SYS
        ora  #$01
        sta  PIA_SYS

        ; --- Fill pixel VRAM with $FF (all foreground) ---
        ldx  #VRAM_BASE
        lda  #$FF
pix_loop:
        sta  ,x+
        cmpx #VRAM_END
        bne  pix_loop

        ; --- Switch to color bank (bit 0 = 0) ---
        lda  PIA_SYS
        anda #$FE
        sta  PIA_SYS

        ; --- Fill color VRAM with $17 (red fg, white bg) ---
        ldx  #VRAM_BASE
        lda  #$17
col_loop:
        sta  ,x+
        cmpx #VRAM_END
        bne  col_loop

        ; --- Halt ---
halt:
        bra  halt

        end  start
