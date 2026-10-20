#!/usr/bin/env bash

# Script create release package.

cd "$(dirname "$0")/.."

get_version() {
    grep -m 1 -oP 'dnx_RTOS_version\s*=\s*"\K[^"]+' src/kernel/syscall.c
}

if [ -z "$(get_version)" ]; then
    echo "Cannot read dnx RTOS version from src/kernel/syscall.c"
    exit 1
fi

GITHASH=$(git rev-parse --short HEAD)
VERSION="dnx-RTOS-v$(get_version)-g${GITHASH}"

git clean -xfd
zip ${VERSION}.zip . -r -9 -x /.git* /*git
zip ${VERSION}.zip .gitignore
echo "Done"
