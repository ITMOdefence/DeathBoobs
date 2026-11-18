#!/bin/sh
set -eu

check_script_dir=$(CDPATH= cd "$(dirname "$0")" && pwd)
check_repo_root=$(CDPATH= cd "$check_script_dir/.." && pwd)
check_punos_root=${PUNOS_ROOT:-"$check_repo_root/../PUNos"}
check_cc=${CC:-cc}
check_nm=${NM:-nm}

for check_header in \
    System/SystemLibs/libcore/include/sys/core.h \
    System/SystemLibs/libstr/include/sys/string.h \
    System/SystemLibs/libmem/include/sys/memory.h \
    System/NativeRuntime/SyscallWrappers/include/punos/syscall/types.h; do
    if [ ! -f "$check_punos_root/$check_header" ]; then
        printf '%s\n' 'PUNos headers not found. Set PUNOS_ROOT to your PUNos checkout.' >&2
        exit 1
    fi
done

check_build_dir=$(mktemp -d "${TMPDIR:-/tmp}/deathboobs-freestanding.XXXXXX")
trap 'rm -rf "$check_build_dir"' EXIT
trap 'exit 1' HUP INT TERM

# Compile real production objects without linking the host libc or test fakes.
# A syntax-only check would miss compiler-generated memcpy/memset references.
for check_level in 0 2; do
    for check_source in codec/record_codec database/database; do
        check_name=${check_source##*/}
        check_object="$check_build_dir/$check_name-O$check_level.o"
        "$check_cc" -std=c11 -Wall -Wextra -Werror -ffreestanding -nostdinc \
            -fno-builtin -fno-stack-protector -fno-pic "-O$check_level" \
            -I "$check_repo_root/include" \
            -I "$check_punos_root/System/SystemLibs/libcore/include" \
            -I "$check_punos_root/System/SystemLibs/libstr/include" \
            -I "$check_punos_root/System/SystemLibs/libmem/include" \
            -I "$check_punos_root/System/NativeRuntime/SyscallWrappers/include" \
            -c "$check_repo_root/src/$check_source.c" -o "$check_object"

        "$check_nm" -u "$check_object" > "$check_build_dir/symbols"
        # Darwin prefixes C symbols with '_'; ELF/GNU nm prints a leading 'U'.
        # Only these public PUNos/DeathBoobs dependencies are permitted.
        awk -v object="$check_object" -v source="$check_source" '
            NF {
                symbol = $NF
                sub(/^_/, "", symbol)
                if (source == "database/database" &&
                    (symbol ~ /^memory_(allocate|reallocate|free)$/ ||
                     symbol ~ /^deathboobs_(game_session|detection)_(encode|decode)$/ ||
                     symbol ~ /^deathboobs_storage_(open|close|page_count|page_read)$/ ||
                     symbol ~ /^deathboobs_transaction_(begin|page_append|commit|rollback)$/)) {
                    next
                }
                print "Unexpected freestanding dependency in " object ": " symbol > "/dev/stderr"
                failed = 1
            }
            END { exit failed }
        ' "$check_build_dir/symbols"
    done
done

printf '%s\n' 'PASS: freestanding objects at -O0/-O2; public dependencies only'
