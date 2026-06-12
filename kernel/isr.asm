; =============================================================================
; RedPandaOS - interrupt stubs
; =============================================================================
; The CPU can't call C functions directly on an interrupt: it only jumps to
; an address from the IDT. These stubs are those addresses. Each one notes
; its interrupt number, then funnels into isr_common, which saves all
; registers, calls the C dispatcher, restores everything and returns with
; iretd.
;
; Some CPU exceptions (8, 10-14, 17, 30) push an error code on the stack;
; for all others we push a dummy 0 so the stack layout - and the C struct
; registers_t in isr.h - is always identical.
; =============================================================================

[bits 32]
section .text

extern interrupt_dispatch

%macro ISR_NOERR 1
isr%1:
    push dword 0                ; dummy error code
    push dword %1               ; interrupt number
    jmp isr_common
%endmacro

%macro ISR_ERR 1
isr%1:                          ; CPU already pushed the real error code
    push dword %1
    jmp isr_common
%endmacro

%macro IRQ 1
irq%1:
    push dword 0
    push dword %1 + 32          ; IRQs are remapped to vectors 32-47
    jmp isr_common
%endmacro

; --- CPU exceptions 0-31 ------------------------------------------------
ISR_NOERR 0                     ; divide error
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6                     ; invalid opcode
ISR_NOERR 7
ISR_ERR   8                     ; double fault
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13                    ; general protection fault
ISR_ERR   14                    ; page fault
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_ERR   30
ISR_NOERR 31

; --- Hardware IRQs 0-15 (vectors 32-47) ---------------------------------
%assign i 0
%rep 16
IRQ i
%assign i i + 1
%endrep

; --- Common entry/exit path ----------------------------------------------
isr_common:
    pusha                       ; eax, ecx, edx, ebx, esp, ebp, esi, edi
    push ds
    push es
    push fs
    push gs

    mov ax, 0x10                ; make sure we run on kernel data segments
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push esp                    ; pointer to the registers_t we just built
    call interrupt_dispatch
    add esp, 4

    pop gs
    pop fs
    pop es
    pop ds
    popa
    add esp, 8                  ; drop int_no + error code
    iretd

; --- Stub address table for idt.c ----------------------------------------
; idt_init() loops over this instead of declaring 48 externs in C.
section .rodata
global isr_stub_table
isr_stub_table:
%assign i 0
%rep 32
    dd isr%+i
%assign i i + 1
%endrep
%assign i 0
%rep 16
    dd irq%+i
%assign i i + 1
%endrep
