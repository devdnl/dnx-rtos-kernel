#!/usr/bin/env bash

# Script builds the kernel for all test configurations located in tools/test_bsp.
# Configurations with drvset partition support (drivers built separately from
# the kernel) are test configurations with overlay (<bsp>+<overlay>: drvset.bsp,
# stm32h7-drvset.bsp also enables the IRQ module).
# The current configuration is restored at the end of the script.

set -e

cd "$(dirname "$0")/.."

CONFIG_BACKUP=$(mktemp -d)
cp -r config/. "$CONFIG_BACKUP"

restore_config()
{
    rm -rf config
    mkdir config
    cp -r "$CONFIG_BACKUP"/. config
    rm -rf "$CONFIG_BACKUP"
    echo "Configuration restored."
}

trap restore_config EXIT

switch_to_bsp()
{
    echo "====================================================================="
    echo "= SWITCHING TO '$1' CONFIGURATION"
    echo "====================================================================="
    make clean
    local bsp
    for bsp in ${1//+/ }; do
        bash ./tools/configtool.sh import=tools/test_bsp/$bsp.bsp:.
    done
}

for bsp in stm32f1 stm32f3 stm32f4 stm32f7 stm32h7 rpcm0 stm32h7+stm32h7-drvset rpcm0+drvset; do
    switch_to_bsp "$bsp"
    make all
done

make clean

echo "All test configurations built successfully."
