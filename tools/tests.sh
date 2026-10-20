#!/usr/bin/env bash

# Script builds the kernel for all test configurations located in tools/test_bsp.
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
    bash ./tools/configtool.sh import=tools/test_bsp/$1.bsp:.
}

for bsp in stm32f1 stm32f3 stm32f4 stm32f7 stm32h7; do
    switch_to_bsp "$bsp"
    make all
done

make clean

echo "All test configurations built successfully."
