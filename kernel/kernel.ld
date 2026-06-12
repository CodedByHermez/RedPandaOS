/* =============================================================================
 * RedPandaOS kernel linker script
 * =============================================================================
 * The bootloader loads the flat kernel binary at physical 0x8000 and jumps
 * to its first byte. So:
 *   - link address = 0x8000 (addresses in the code match where it runs)
 *   - the .entry section (entry.asm) MUST come first: byte 0 is _start
 *   - .bss is NOLOAD: it occupies no space in the flat binary; entry.asm
 *     zeroes [__bss_start, __bss_end) before calling kmain
 * ============================================================================= */

ENTRY(_start)

SECTIONS
{
    . = 0x8000;

    .entry : { KEEP(*(.entry)) }

    .text   : { *(.text*) }
    .rodata : { *(.rodata*) }
    .data   : { *(.data*) }

    .bss (NOLOAD) :
    {
        __bss_start = .;
        *(COMMON)
        *(.bss*)
        __bss_end = .;
    }

    __kernel_end = .;

    /DISCARD/ : { *(.eh_frame*) *(.comment) *(.note*) }
}
