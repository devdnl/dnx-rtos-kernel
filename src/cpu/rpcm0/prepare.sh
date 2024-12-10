#!/usr/bin/env bash

projroot=$(pwd)
rpcm0dir="$(dirname $(realpath $0))"

cd "${rpcm0dir}/boot_stage2"
./build.sh
