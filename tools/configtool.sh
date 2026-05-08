#!/usr/bin/env sh

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
sh "${SCRIPT_DIR}/configtool-cli/configtool-cli" $@
