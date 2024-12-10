#!/usr/bin/env bash

AS_FLAGS="-x assembler-with-cpp -mcpu=cortex-m0plus -O3 -nostdlib -ffreestanding -mthumb"
AS_INCLUDE="-I./asminclude -I./ -I../hardware_regs/include"

function build_binary() {
    file_asm="$1"

    echo "Building ${file_asm}..."
    arm-none-eabi-gcc ${AS_FLAGS} ${AS_INCLUDE} "${file_asm}" -o build/${file_asm%.*}.elf
    #arm-none-eabi-objdump -S build/${file_asm%.*}.elf > build/${file_asm%.*}.lst
    arm-none-eabi-objcopy build/${file_asm%.*}.elf -O binary build/${file_asm%.*}.bin
    ./pad_checksum -s 0xFFFFFFFF build/${file_asm%.*}.bin build/${file_asm%.*}
    rm build/${file_asm%.*}.elf
}

rm -rf build
mkdir build -p
build_binary "boot2_w25q080.S"
build_binary "boot2_at25sf128a.S"
build_binary "boot2_generic_03h.S"
build_binary "boot2_is25lp080.S"
build_binary "boot2_w25q080.S"
build_binary "boot2_w25x10cl.S"
