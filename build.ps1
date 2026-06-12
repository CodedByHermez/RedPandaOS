# =============================================================================
# RedPandaOS build script
# =============================================================================
# Pipeline:
#   1. NASM  boot/boot.asm     -> build/boot.bin     (flat 512-byte boot sector)
#   2. NASM  kernel/entry.asm  -> build/entry.o      (ELF32 object)
#   3. CC    kernel/*.c        -> build/*.o          (freestanding i686 ELF)
#   4. LD + kernel.ld          -> build/kernel.elf   (entry stub linked first)
#   5. objcopy -O binary       -> build/kernel.bin   (flat binary at 0x8000)
#   6. pad kernel to $KernelSectors sectors, concat  -> build/RedPandaOS.img
#
#   .\build.ps1          build only
#   .\build.ps1 -Run     build, then boot the image in QEMU
# =============================================================================

param([switch]$Run)

$ErrorActionPreference = 'Stop'
$root  = $PSScriptRoot
$build = Join-Path $root 'build'
$qemu  = 'C:\Program Files\qemu\qemu-system-i386.exe'

# Single source of truth for the kernel size budget. The bootloader reads
# exactly this many sectors (passed to NASM via -D), and we pad the kernel
# binary to exactly this size. If you change this, update FS_START_LBA in
# kernel\fs.h (= 1 + KernelSectors) to match.
$KernelSectors = 192
$KernelMaxBytes = $KernelSectors * 512

# Total disk image size. Everything after boot sector + kernel belongs to
# the filesystem and SURVIVES rebuilds (we only rewrite the kernel region).
$ImageBytes = 16MB

# Cross toolchain. Preferred: portable i686-elf-gcc (no admin install needed,
# lives in LOCALAPPDATA). If Clang/LLVM is on PATH it works too.
$elfTools = "$env:LOCALAPPDATA\i686-elf-tools\bin"
if (Test-Path "$elfTools\i686-elf-gcc.exe") {
    $cc      = "$elfTools\i686-elf-gcc.exe"
    $ld      = "$elfTools\i686-elf-ld.exe"
    $objcopy = "$elfTools\i686-elf-objcopy.exe"
    $ccTargetFlags = @('-march=i686')
    $ldFlags = @()
} elseif (Get-Command clang -ErrorAction SilentlyContinue) {
    $cc      = 'clang'
    $ld      = 'ld.lld'
    $objcopy = 'llvm-objcopy'
    $ccTargetFlags = @('--target=i686-pc-none-elf', '-march=i686')
    $ldFlags = @('-m', 'elf_i386')
} else {
    Write-Error 'No cross compiler found: install i686-elf-tools or LLVM/Clang'
}

$cflags = $ccTargetFlags + @(
    '-ffreestanding', '-fno-builtin', '-nostdlib',
    '-fno-pie', '-fno-pic',
    '-fno-stack-protector', '-fno-asynchronous-unwind-tables',
    '-mno-sse', '-mno-sse2', '-mno-mmx', '-msoft-float',
    '-O2', '-Wall', '-Wextra'
)

New-Item -ItemType Directory -Force $build | Out-Null

Write-Host '[1/6] Assembling bootloader...'
nasm -f bin (Join-Path $root 'boot\boot.asm') "-DKERNEL_SECTORS=$KernelSectors" -o (Join-Path $build 'boot.bin')
if ($LASTEXITCODE -ne 0) { exit 1 }
$bootSize = (Get-Item (Join-Path $build 'boot.bin')).Length
if ($bootSize -ne 512) { Write-Error "boot.bin is $bootSize bytes, expected 512" }

Write-Host '[2/6] Assembling kernel asm...'
$objects = @()
# entry.asm first so _start is byte 0 (kernel.ld pins .entry there anyway)
$asmSources = @(Get-Item (Join-Path $root 'kernel\entry.asm')) +
              @(Get-ChildItem (Join-Path $root 'kernel') -Filter '*.asm' | Where-Object Name -ne 'entry.asm')
foreach ($src in $asmSources) {
    # .asm.o suffix so isr.asm and isr.c don't fight over the same object file
    $obj = Join-Path $build ($src.BaseName + '.asm.o')
    nasm -f elf32 $src.FullName -o $obj
    if ($LASTEXITCODE -ne 0) { exit 1 }
    $objects += $obj
}

Write-Host '[3/6] Compiling C kernel...'
foreach ($src in Get-ChildItem (Join-Path $root 'kernel') -Filter '*.c') {
    $obj = Join-Path $build ($src.BaseName + '.o')
    & $cc @cflags -c $src.FullName -o $obj
    if ($LASTEXITCODE -ne 0) { exit 1 }
    $objects += $obj
}

Write-Host '[4/6] Linking kernel...'
& $ld @ldFlags -T (Join-Path $root 'kernel\kernel.ld') -o (Join-Path $build 'kernel.elf') @objects
if ($LASTEXITCODE -ne 0) { exit 1 }

Write-Host '[5/6] Flattening to binary...'
& $objcopy -O binary (Join-Path $build 'kernel.elf') (Join-Path $build 'kernel.bin')
if ($LASTEXITCODE -ne 0) { exit 1 }

Write-Host '[6/6] Creating disk image...'
$kernelBytes = [IO.File]::ReadAllBytes((Join-Path $build 'kernel.bin'))
if ($kernelBytes.Length -gt $KernelMaxBytes) {
    Write-Error ("Kernel is $($kernelBytes.Length) bytes but the bootloader only loads " +
                 "$KernelMaxBytes - raise `$KernelSectors in build.ps1")
}
$bootBytes = [IO.File]::ReadAllBytes((Join-Path $build 'boot.bin'))
$image     = Join-Path $build 'RedPandaOS.img'

# Reuse the existing image so files on the RPFS filesystem survive rebuilds;
# only the boot sector + kernel region is rewritten.
if ((Test-Path $image) -and ((Get-Item $image).Length -eq $ImageBytes)) {
    $imageData = [IO.File]::ReadAllBytes($image)
    $fsNote = 'kept (files survive rebuilds)'
} else {
    $imageData = New-Object byte[] $ImageBytes
    $fsNote = 'fresh (kernel formats it on first boot)'
}
[Array]::Copy($bootBytes, 0, $imageData, 0, $bootBytes.Length)
$kernelRegion = New-Object byte[] $KernelMaxBytes    # zero-padded
[Array]::Copy($kernelBytes, $kernelRegion, $kernelBytes.Length)
[Array]::Copy($kernelRegion, 0, $imageData, 512, $KernelMaxBytes)
[IO.File]::WriteAllBytes($image, $imageData)

Write-Host ''
Write-Host "  boot sector : $bootSize bytes"
Write-Host "  kernel      : $($kernelBytes.Length) / $KernelMaxBytes bytes used"
Write-Host "  filesystem  : $(($ImageBytes - 512 - $KernelMaxBytes) / 1KB) KB region - $fsNote"
Write-Host "  image       : $image ($((Get-Item $image).Length / 1MB) MB)"

if ($Run) {
    Write-Host 'Booting in QEMU...'
    & $qemu -drive format=raw,file=$image
}
