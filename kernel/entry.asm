; =============================================================================
; RedPandaOS - Kernel entry stub
; =============================================================================
; The bootloader jumps here at 0x0000:0x8000 in 16-bit real mode. This is the
; bridge between the BIOS world and our C kernel:
;
;   1. Enable the A20 address line (otherwise address bit 20 is forced to 0,
;      a compatibility relic of the 8086's 1MB wraparound).
;   2. Collect the BIOS E820 memory map - this MUST happen now, while BIOS
;      services still exist; protected mode has no way to ask for it.
;   3. Load a GDT describing flat 4GB code and data segments.
;   4. Set CR0.PE and far-jump into 32-bit protected mode.
;   5. Reload segment registers, set up the stack, zero the BSS.
;   6. Call kmain() - from there on, everything is C.
;
; Assembled to ELF32 and placed first in the kernel binary by kernel.ld,
; so byte 0 of kernel.bin is exactly `_start`.
; =============================================================================

section .entry
global _start
extern kmain
extern __bss_start
extern __bss_end

; Low-memory scratch layout. Everything the kernel needs from the BIOS world
; is parked at fixed addresses before we leave real mode:
;   0x6000  E820 memory map     (dword count + up to 32 x 24-byte entries)
;   0x6400  video info          (width/height/bpp/pitch/framebuffer address)
;   0x6800  ROM 8x16 font       (256 glyphs x 16 bytes = 4 KB)
;   0x7800  VBE mode info       (256-byte scratch, only used here)
E820_BUF         equ 0x6000
E820_MAX_ENTRIES equ 32
VIDEO_INFO       equ 0x6400
FONT_BUF         equ 0x6800
VBE_MODEINFO     equ 0x7800

[bits 16]
_start:
    cli                         ; interrupts stay off until we have an IDT (v3)
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov sp, 0x6000              ; real-mode stack below the scratch area
    cld

    ; --- Enable A20 via the "fast gate" (port 0x92) ---------------------
    ; Read-modify-write; never touch bit 0 (it resets the CPU!).
    in  al, 0x92
    test al, 2
    jnz .a20_done               ; already enabled (QEMU usually does)
    or  al, 2
    and al, 0xFE
    out 0x92, al
.a20_done:

    ; --- Collect the E820 memory map (BIOS int 15h, EAX=E820) ----------
    xor ebx, ebx                ; continuation value, 0 = start over
    xor bp, bp                  ; entry counter
    mov di, E820_BUF + 4        ; entries start after the count dword
.e820_loop:
    mov eax, 0xE820
    mov edx, 0x534D4150         ; 'SMAP' magic
    mov ecx, 24                 ; ask for 24-byte (ACPI 3.0) entries
    mov dword [di + 20], 1      ; preset ext. attributes: entry valid
    int 0x15
    jc  .e820_done              ; carry = unsupported or end of map
    cmp eax, 0x534D4150         ; BIOS echoes 'SMAP' on success
    jne .e820_done
    inc bp
    add di, 24
    cmp bp, E820_MAX_ENTRIES
    jae .e820_done
    test ebx, ebx               ; EBX=0 means that was the last entry
    jnz .e820_loop
.e820_done:
    mov [E820_BUF], bp
    mov word [E820_BUF + 2], 0

    ; --- Copy the ROM 8x16 font to our buffer ---------------------------
    ; Graphics mode has no hardware text - we render every glyph ourselves,
    ; and the VGA ROM already contains a perfectly good bitmap font.
    mov ax, 0x1130
    mov bh, 0x06                ; request the 8x16 ROM font
    int 0x10                    ; returns pointer in ES:BP
    mov si, bp
    mov ax, es
    mov ds, ax                  ; DS:SI = ROM font
    xor ax, ax
    mov es, ax
    mov di, FONT_BUF            ; ES:DI = our buffer
    mov cx, 4096 / 2
    rep movsw
    xor ax, ax
    mov ds, ax                  ; DS back to 0

    ; --- Pick and set a VBE linear-framebuffer mode ----------------------
    ; Walk the candidate list (best first); take the first mode that the
    ; BIOS supports with a linear framebuffer at 32 or 24 bpp.
    mov si, vbe_modes
.vbe_try:
    lodsw
    test ax, ax
    jz  .vbe_fail               ; list exhausted
    mov [vbe_mode], ax
    mov cx, ax
    mov ax, 0x4F01              ; VBE: get mode info
    mov di, VBE_MODEINFO        ; ES:DI = info buffer (ES = 0)
    int 0x10
    cmp ax, 0x004F              ; AL=4F: supported, AH=00: success
    jne .vbe_try
    mov al, [VBE_MODEINFO]      ; ModeAttributes
    and al, 0x81                ; bit 0: supported, bit 7: linear framebuffer
    cmp al, 0x81
    jne .vbe_try
    mov al, [VBE_MODEINFO + 0x19]
    cmp al, 32                  ; bits per pixel
    je  .vbe_set
    cmp al, 24
    jne .vbe_try
.vbe_set:
    mov bx, [vbe_mode]
    or  bx, 0x4000              ; bit 14: use the linear framebuffer
    mov ax, 0x4F02              ; VBE: set mode
    int 0x10
    cmp ax, 0x004F
    jne .vbe_fail

    ; Stash what the kernel needs (videoinfo.h mirrors this layout).
    mov ax, [VBE_MODEINFO + 0x12]
    mov [VIDEO_INFO], ax        ; width
    mov ax, [VBE_MODEINFO + 0x14]
    mov [VIDEO_INFO + 2], ax    ; height
    movzx ax, byte [VBE_MODEINFO + 0x19]
    mov [VIDEO_INFO + 4], ax    ; bpp
    mov word [VIDEO_INFO + 6], 0
    movzx eax, word [VBE_MODEINFO + 0x10]
    mov [VIDEO_INFO + 8], eax   ; pitch (bytes per scanline)
    mov eax, [VBE_MODEINFO + 0x28]
    mov [VIDEO_INFO + 12], eax  ; physical framebuffer address
    jmp .video_done

.vbe_fail:
    ; Still in text mode, so say so the old way and stop.
    mov ax, 0xB800
    mov es, ax
    mov word [es:0], 0x4F56     ; 'V', white on red
.vbe_hang:
    hlt
    jmp .vbe_hang

.video_done:

    ; --- Enter protected mode -------------------------------------------
    lgdt [gdt_descriptor]
    mov eax, cr0
    or  eax, 1                  ; CR0.PE = protection enable
    mov cr0, eax
    jmp dword 0x08:pm_start     ; far jump: flush prefetch queue, load 32-bit CS

[bits 32]
pm_start:
    ; --- Fresh flat 32-bit world ----------------------------------------
    mov ax, 0x10                ; data segment selector
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000            ; stack: grows down from 0x90000, safely below
                                ; the Extended BIOS Data Area (~0x9FC00)

    ; --- Zero the BSS so C globals start at 0, as C guarantees ----------
    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    xor eax, eax
    rep stosb

    ; --- Into C. Never returns. -----------------------------------------
    call kmain

.hang:
    hlt
    jmp .hang

; =============================================================================
; GDT: null descriptor + flat 4GB code segment + flat 4GB data segment
; =============================================================================
align 8
gdt_start:
    dq 0                        ; 0x00: mandatory null descriptor

    ; 0x08: code - base 0, limit 0xFFFFF pages (4GB), ring 0, exec/read
    dw 0xFFFF                   ; limit  [0:15]
    dw 0x0000                   ; base   [0:15]
    db 0x00                     ; base   [16:23]
    db 10011010b                ; access: present, ring 0, code, exec/read
    db 11001111b                ; flags: 4KB granularity, 32-bit; limit [16:19]
    db 0x00                     ; base   [24:31]

    ; 0x10: data - same but writable data instead of code
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 10010010b                ; access: present, ring 0, data, read/write
    db 11001111b
    db 0x00
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1  ; GDT size - 1
    dd gdt_start                ; GDT linear address

; VBE mode candidates, best first. Both the Bochs/QEMU 32bpp numbers (0x14x)
; and the classic VESA 24bpp numbers (0x11x) are covered:
; 1024x768x32, 1024x768x24, 800x600x32, 800x600x24, 640x480x32, 640x480x24.
vbe_modes:
    dw 0x144, 0x118, 0x143, 0x115, 0x142, 0x112, 0
vbe_mode:
    dw 0
