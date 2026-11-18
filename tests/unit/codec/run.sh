#!/bin/sh
set -eu

codec_script_dir=$(CDPATH= cd "$(dirname "$0")" && pwd)
codec_repo_root=$(CDPATH= cd "$codec_script_dir/../../.." && pwd)
codec_punos_root=${PUNOS_ROOT:-"$codec_repo_root/../PUNos"}
codec_cc=${CC:-cc}

if [ ! -f "$codec_punos_root/System/SystemLibs/libcore/include/sys/core.h" ] ||
   [ ! -f "$codec_punos_root/System/NativeRuntime/SyscallWrappers/include/punos/syscall/types.h" ]; then
    printf '%s\n' 'PUNos headers not found. Set PUNOS_ROOT to your PUNos checkout.' >&2
    exit 1
fi

codec_build_dir=$(mktemp -d "${TMPDIR:-/tmp}/deathboobs-codec.XXXXXX")
trap 'rm -rf "$codec_build_dir"' EXIT
trap 'exit 1' HUP INT TERM

if [ "${DEATHBOOBS_CODEC_SANITIZE:-0}" = 1 ]; then
    set -- -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer
else
    set -- -O2
fi

"$codec_cc" -std=c11 -Wall -Wextra -Werror -ffreestanding "$@" \
    -I "$codec_repo_root/include" \
    -I "$codec_punos_root/System/SystemLibs/libcore/include" \
    -I "$codec_punos_root/System/NativeRuntime/SyscallWrappers/include" \
    "$codec_repo_root/src/codec/record_codec.c" \
    "$codec_script_dir/record_codec_test.c" \
    -o "$codec_build_dir/record_codec_test"

"$codec_build_dir/record_codec_test"
