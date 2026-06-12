# RedPandaOS Roadmap 🐾

North star: **a graphical desktop with windows and a mouse cursor — drawn entirely
by code we wrote ourselves.** Each version below is one self-contained, bootable
milestone on the way there.

## ✅ v1 — Hello World (done)

Our own boot sector loads our own 16-bit kernel, which writes
`RedPanda: Hello World!` directly into VGA text memory. 1024 bytes total.

## ✅ v2 — Protected mode + C kernel (done)

The foundation for everything that follows.

- Bootloader upgraded: loads a 32 KB kernel via BIOS LBA reads.
- Kernel entry stub (assembly): enable the A20 line, set up a GDT, switch the
  CPU into **32-bit protected mode**, zero the BSS, call `kmain()`.
- Kernel is written in **C** from here on (freestanding — no libc, we write
  every function ourselves). Assembly remains only for what C can't do.
- A real VGA text driver: print, newline handling, scrolling, 16 colors,
  hardware cursor control.
- Toolchain: NASM + Clang/LLVM (cross-compiling to bare-metal i686).

## ✅ v3 — Interrupts & input (done)

The OS starts *reacting* to the world.

- IDT with CPU exception handlers — a panic screen with a register dump
  instead of a silent triple-fault reboot (try the `crash` command).
- PIC remapped, hardware IRQs working.
- PIT timer ticking at 100 Hz (`uptime`).
- PS/2 keyboard driver: scancodes → ASCII, shift + caps lock.
- `kprintf` with format strings (%s %c %d %u %x %p, %08x padding).
- Payoff: the `redpanda>` shell — type and the OS answers.

## ✅ v4 — Memory management (done)

A GUI needs to allocate windows, buffers, and queues at runtime.

- BIOS E820 memory map, read in the real-mode stub before protected mode
  (`mem` shows it).
- Physical frame allocator: bitmap of 4 KB frames over all usable RAM.
- Paging enabled — RAM identity-mapped, page faults caught with the
  faulting address (`pagefault` demos it).
- Kernel heap at virtual `0xD0000000`: our own `kmalloc`/`kfree` with block
  splitting, coalescing, and on-demand growth (`memtest` exercises it).

## ✅ v5 — Graphics 🎨 (done)

Goodbye 80×25 text mode.

- VBE linear framebuffer negotiated in real mode: 1024×768, 32 bpp
  (graceful fallback list down to 640×480×24).
- Framebuffer driver: pixels, rects, outlines, Bresenham lines, text.
- Font renderer using the ROM 8×16 font (copied out before the BIOS dies);
  the whole console — shell, boot log, panic screen — is now rendered
  pixel by pixel.
- Double buffering on the v4 heap, with per-cell partial flips so typing
  stays snappy.
- `gfx` command: gradient, rect art, starburst — and the resident red panda.

## ✅ v6 — Mouse (done)

- PS/2 mouse driver: IRQ 12, 200 Hz sample rate, mild acceleration curve
  (precise when slow, fast when flicked).
- The cursor: classic arrow with alpha-blended soft drop shadow,
  composited over the framebuffer at flip time — always on top, can't be
  drawn over, moving repaints only two tiny rects. Smooth.
- Unified input event queue (keyboard + mouse) — the future GUI event loop.
- `paint` command: draw with the mouse, live coordinates, right-click clears.

## ✅ v7 — The GUI 🏔️ (done)

Everything above converges here.

- Desktop: teal gradient wallpaper with soft color glows, rendered once
  into a cached background layer.
- Compositor: background restore → windows bottom-to-top → frosted taskbar
  → flip; input events coalesced per frame for smooth dragging.
- Windows: rounded corners, soft drop shadows, gradient title bars,
  macOS-style traffic lights (close / minimize), drag, z-order, focus.
- Taskbar: paw-print start orb, per-window buttons with focus accent,
  live RTC clock (CMOS driver).
- Starter apps: Welcome (with the panda) and System (live stats).
- ESC drops to the text console; `gui` returns to the desktop.

## ✅ v8 — Filesystem (done)

- ATA PIO disk driver (LBA28 read/write/identify, cache flush on write).
- **RPFS**, our own on-disk format: superblock + FAT-style block chains +
  flat root directory (64 files). Lives on the boot disk after the kernel.
- Files survive reboots AND rebuilds (the build keeps the FS region).
- The kernel formats the region automatically on first boot.
- Bootloader now loads up to 96 KB of kernel in 64 KB-safe chunks.
- Polish: window shadows hug the rounded corners (corner bug fixed, with
  edge anti-aliasing), desktop starts empty with `RedPandaOS 1.0` branding,
  console is developer-only now (hidden key, no user access).
- Dev console: `ls`, `cat`, `write`, `rm`, `demo`.

## ✅ v9 — Desktop icons & trash bin (done)

The desktop comes alive.

- Every RPFS file is a desktop icon (document glyph + label), laid out in a
  column grid that refreshes live as files change.
- Selection: click an icon, or hold left-click on empty desktop and sweep a
  rubber-band rectangle — icons highlight as the band touches them.
- Drag & drop: dragging icons shows a ghost document (with a count badge
  for multi-selections); dropping on the trash deletes them.
- The trash bin: on-disk (trashed files are renamed to a hidden prefix, so
  the trash survives reboots), shows crumpled paper when full. Double-click
  opens the Trash window: click a file to restore it, or Empty Trash.
- Double-clicking a file opens a read-only viewer window with its contents.
- Windows grew per-window content click handlers and owned text buffers;
  RPFS grew `fs_rename`.

## ✅ v10 — Start menu & the dynamic desktop (done)

The desktop stops being a grid and starts being yours.

- **Free placement**: icons and the trash bin drop anywhere on the desktop
  (multi-selections move together, with a translucent preview of where each
  icon will land). The arrangement persists in a hidden layout file on RPFS
  and survives reboots; new files take the first free grid cell.
- **The start menu**: the paw orb opens a rounded, gradient panel that
  slides up out of the orb — panda header, app rows (Welcome, System
  Monitor, Trash), then About / Restart / Shut Down. Hover rows highlight
  with an accent bar; every glyph is drawn with our own primitives.
- **About**: a proper About window — the panda, the version, the creed.
- **Restart**: goodbye screen, then a real reset via the 8042 keyboard
  controller (triple-fault fallback).
- **Shut Down**: goodbye screen, then ACPI poweroff (QEMU / Bochs /
  VirtualBox ports), falling back to "safe to turn off" + halt on hardware.
- ESC closes the menu; clicking outside swallows the click, like the
  classics.

## ✅ v11 — The text editor (done)

The first real app: double-click any file on the desktop and edit it.

- A full editing buffer: typing inserts at the caret, Enter / Backspace /
  Delete / Tab, arrow keys (up and down remember the preferred column),
  Home / End, click anywhere to place the caret, auto-scroll in both axes.
- **Ctrl+S** saves to RPFS (new files appear on the desktop instantly),
  **Ctrl+Z** undo / **Ctrl+Y** redo — snapshot-based, with bursts of
  same-kind edits coalescing so undo takes back a word, not a letter.
- Status bar: line/column, save state ("modified — ^S saves" in accent).
- Start menu grew a **Text Editor** entry (pencil glyph) that opens a fresh
  `untitled.txt`, created on disk on first save.
- Under the hood: the keyboard driver learned extended scancodes (arrows,
  Home/End/Delete) and Ctrl combos; windows route keyboard input to the
  focused app via a `key` handler and own their state via `on_close`.
- Bonus: pressing Delete on the desktop moves selected icons to the trash.

## Where we're heading next

The plan, roughly in order — each one lands as its own milestone:

- Later still: userspace (ring 3, syscalls), apps loaded from disk,
  and MUCH more.
