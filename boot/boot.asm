; =============================================================================
; RedPandaOS - Stage 1 Bootloader
; =============================================================================
; The BIOS loads this 512-byte sector from the first sector of the boot disk
; to physical address 0x7C00 and jumps to it in 16-bit real mode.
;
; Our job here:
;   1. Set up segment registers and a stack (the BIOS guarantees nothing).
;   2. Load the kernel (KERNEL_SECTORS sectors, starting at disk sector 2)
;      to 0x8000 using BIOS int 13h LBA extensions.
;   3. Jump to the kernel's entry stub, still in real mode.
;
; KERNEL_SECTORS is injected by build.ps1 via "nasm -DKERNEL_SECTORS=N" so the
; build script is the single source of truth for the kernel size budget.
;
; The kernel is read in chunks of 64 sectors (32 KB). Each chunk goes to a
; segment that is a multiple of 0x800, so no single BIOS transfer ever
; crosses a 64 KB physical boundary (which int 13h does not allow).
; =============================================================================

[org 0x7C00]
[bits 16]

%ifndef KERNEL_SECTORS
%define KERNEL_SECTORS 192
%endif

KERNEL_OFFSET equ 0x8000        ; where the kernel lands in memory
CHUNK_SECTORS equ 64            ; 32 KB per BIOS read

start:
    ; --- Set up a known-good environment -------------------------------
    cli                         ; no interrupts while we move the stack
    xor ax, ax
    mov ds, ax                  ; data segment    = 0
    mov es, ax                  ; extra segment   = 0
    mov ss, ax                  ; stack segment   = 0
    mov sp, 0x7C00              ; stack grows down from just below us
    sti

    mov [boot_drive], dl        ; BIOS hands us the boot drive number in DL

    ; --- Load the kernel from disk (int 13h AH=42, LBA reads) ----------
    ; Chunked loop: 64 sectors to 0x0800:0000 (=0x8000), 64 more to
    ; 0x1000:0000 (=0x10000), and so on until KERNEL_SECTORS are in.
    mov cx, KERNEL_SECTORS      ; sectors remaining
.load_loop:
    mov ax, cx
    cmp ax, CHUNK_SECTORS
    jbe .count_ok
    mov ax, CHUNK_SECTORS
.count_ok:
    mov [dap_count], ax
    push cx
    push ax
    mov si, dap
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    pop ax
    pop cx
    jc  disk_error              ; carry flag set = read failed
    sub cx, ax
    jz  .load_done
    add word [dap_lba], CHUNK_SECTORS
    add word [dap_segment], 0x800       ; next 32 KB window
    jmp .load_loop
.load_done:

    ; --- Hand over control to the kernel's entry stub ------------------
    jmp 0x0000:KERNEL_OFFSET

disk_error:
    ; Write a red 'D' directly into VGA text memory so we can see what
    ; went wrong even without a working kernel, then halt forever.
    mov ax, 0xB800
    mov es, ax
    mov word [es:0], 0x4F44     ; 'D', white on red
hang:
    hlt
    jmp hang

; --- Disk Address Packet for int 13h AH=42 -----------------------------
align 4
dap:
    db 0x10                     ; packet size
    db 0                        ; reserved
dap_count:
    dw 0                        ; sectors to read (set per chunk)
    dw 0x0000                   ; destination offset (segment:0)
dap_segment:
    dw 0x0800                   ; destination segment (0x0800 = 0x8000)
dap_lba:
    dq 1                        ; starting LBA (sector 0 is this boot sector)

boot_drive: db 0

; --- Boot sector magic -------------------------------------------------
; Pad to 510 bytes, then the 0xAA55 signature the BIOS looks for.
times 510 - ($ - $$) db 0
dw 0xAA55
