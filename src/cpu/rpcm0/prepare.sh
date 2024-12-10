#!/usr/bin/env bash

projroot=$(pwd)
rpcm0dir="$(dirname $(realpath $0))"

echo "Root: ${projroot}"

cd "${rpcm0dir}/boot_stage2"
./build.sh
find . -name "*.bin" -exec cp {} "${projroot}/build/output/" \;
