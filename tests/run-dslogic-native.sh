#!/usr/bin/env bash
# Standalone hardware-free regression runner. Override GCC and DEPS as needed.
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$PWD"
DEPS="${DEPS:-$ROOT/../build-dsl/deps/root/mingw64}"
GCC="${GCC:-/c/gcc/gcc_win/gcc-16.1.0/bin/gcc}"
export PATH="$(dirname "$GCC"):$DEPS/bin:$PATH"
OUT="${OUT:-$ROOT/tests/dslogic-native-build}"
mkdir -p "$OUT"
FLAGS=(-std=gnu11 -O2 -g -Wall -Wextra -Werror=implicit-function-declaration
  -ffunction-sections -fdata-sections -DHAVE_LIBUSB_1_0 -DHAVE_LIBUSB_OS_HANDLE
  -DHAVE_SERIAL_COMM -DHAVE_ZLIB -DHAVE_LIBZIP -DHAVE_DRIVERS -D_GNU_SOURCE
  -I"$ROOT/src" -I"$ROOT/include" -I"$ROOT/../libusb/libusb" -I"$ROOT/../common"
  -I"$DEPS/include" -I"$DEPS/include/glib-2.0" -I"$DEPS/lib/glib-2.0/include")
for name in api command protocol; do
  "$GCC" "${FLAGS[@]}" -c "$ROOT/src/hardware/dreamsourcelab-dslogic/$name.c" -o "$OUT/$name.o"
done
if [[ -f "$ROOT/tests/dslogic-native.c" ]]; then
  "$GCC" "${FLAGS[@]}" "$ROOT/tests/dslogic-native.c" -Wl,--gc-sections "$DEPS/lib/libglib-2.0.dll.a" -lm -o "$OUT/dslogic-native.exe"
  "$OUT/dslogic-native.exe"
fi
