#!/bin/sh
set -eu

deathboobs_test_dir=$(CDPATH= cd "$(dirname "$0")" && pwd)
if [ "${DEATHBOOBS_SANITIZE:-0}" = 1 ]; then
    export DEATHBOOBS_CODEC_SANITIZE=1
    export DEATHBOOBS_DATABASE_SANITIZE=1
fi
sh "$deathboobs_test_dir/check_freestanding.sh"
sh "$deathboobs_test_dir/unit/codec/run.sh"
sh "$deathboobs_test_dir/unit/database/run.sh"
