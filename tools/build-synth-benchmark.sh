#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
usage() {
    echo 'Usage: tools/build-synth-benchmark.sh [--host | --abi arm64-v8a|armeabi-v7a|x86_64]'
    echo 'Without --abi, builds host using CXX (default c++). Does not run adb.'
}
abi=''
if [[ $# -eq 1 && $1 == --help ]]; then usage; exit 0; fi
if [[ $# -eq 1 && $1 == --host ]]; then shift; fi
if [[ $# -ne 0 ]]; then
    if [[ $# -ne 2 || $1 != --abi ]]; then usage >&2; exit 2; fi
    abi=$2
    case "$abi" in arm64-v8a|armeabi-v7a|x86_64) ;; *) usage >&2; exit 2 ;; esac
fi
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
compiler=${CXX:-c++}
flags=()
target=host
if [[ -n $abi ]]; then
    ndk=${ANDROID_NDK_ROOT:-${ANDROID_NDK_HOME:-}}
    if [[ -z $ndk ]]; then
        sdk=${ANDROID_SDK_ROOT:-${ANDROID_HOME:-/workspace/toolchains/android-sdk}}
        ndk=$sdk/ndk/27.2.12479018
    fi
    case "$(uname -s)" in Linux) host=linux-x86_64 ;; Darwin) host=darwin-x86_64 ;; *) echo 'Unsupported NDK build host' >&2; exit 1 ;; esac
    case "$abi" in
        arm64-v8a) triple=aarch64-linux-android ;;
        armeabi-v7a) triple=armv7a-linux-androideabi ;;
        x86_64) triple=x86_64-linux-android ;;
    esac
    compiler=$ndk/toolchains/llvm/prebuilt/$host/bin/${triple}29-clang++
    if [[ ! -x $compiler ]]; then echo "NDK compiler missing: $compiler (set ANDROID_NDK_ROOT or ANDROID_NDK_HOME)" >&2; exit 1; fi
    flags+=(-static-libstdc++)
    target=$abi
fi
out=$root/build/synth-benchmark/$target
mkdir -p "$out"
src=$root/third_party/yamaha-smaf-player/src
"$compiler" -std=c++20 -O3 -DNDEBUG -Wall -Wextra "${flags[@]}" -I"$src" \
    "$root/tools/synth-benchmark.cpp" "$src/ma_fm_core.cpp" "$src/ma_player.cpp" \
    "$src/smaf_file.cpp" "$src/smaf_voice.cpp" -o "$out/synth-benchmark"
printf '%s\n' "$out/synth-benchmark"
