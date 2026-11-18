#!/bin/sh
set -eu

database_script_dir=$(CDPATH= cd "$(dirname "$0")" && pwd)
database_repo_root=$(CDPATH= cd "$database_script_dir/../../.." && pwd)
database_punos_root=${PUNOS_ROOT:-"$database_repo_root/../PUNos"}
database_cc=${CC:-cc}

if [ ! -f "$database_punos_root/System/SystemLibs/libcore/include/sys/core.h" ] ||
   [ ! -f "$database_punos_root/System/SystemLibs/libstr/include/sys/string.h" ] ||
   [ ! -f "$database_punos_root/System/SystemLibs/libmem/include/sys/memory.h" ] ||
   [ ! -f "$database_punos_root/System/NativeRuntime/SyscallWrappers/include/punos/syscall/types.h" ]; then
    printf '%s\n' 'PUNos headers not found. Set PUNOS_ROOT to your PUNos checkout.' >&2
    exit 1
fi

database_build_dir=$(mktemp -d "${TMPDIR:-/tmp}/deathboobs-database.XXXXXX")
trap 'rm -rf "$database_build_dir"' EXIT
trap 'exit 1' HUP INT TERM

if [ "${DEATHBOOBS_DATABASE_SANITIZE:-0}" = 1 ]; then
    set -- -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer
else
    set -- -O2
fi

"$database_cc" -std=c11 -Wall -Wextra -Werror -ffreestanding "$@" \
    -I "$database_repo_root/include" \
    -I "$database_repo_root/tests/fakes" \
    -I "$database_punos_root/System/SystemLibs/libcore/include" \
    -I "$database_punos_root/System/SystemLibs/libstr/include" \
    -I "$database_punos_root/System/SystemLibs/libmem/include" \
    -I "$database_punos_root/System/NativeRuntime/SyscallWrappers/include" \
    "$database_repo_root/src/codec/record_codec.c" \
    "$database_repo_root/src/database/database.c" \
    "$database_repo_root/tests/fakes/fake_memory.c" \
    "$database_repo_root/tests/fakes/fake_storage.c" \
    "$database_repo_root/tests/fakes/fake_transaction.c" \
    "$database_script_dir/database_test.c" \
    -o "$database_build_dir/database_test"

"$database_build_dir/database_test"
