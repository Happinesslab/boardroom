#!/bin/sh
# Builds src/live_stub.bin: hook.S at 0x000, run_requests at 0x100, no
# relocations (checked). Needs mingw-w64 (x86_64-w64-mingw32-gcc).
set -eu
cd "$(dirname "$0")"
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
x86_64-w64-mingw32-gcc -c -O2 -ffreestanding -fno-builtin -fno-stack-protector -fno-stack-check \
    -fno-asynchronous-unwind-tables -fno-exceptions -fno-ident -mno-sse -Wall -Wextra -Werror \
    -o "$out/stub.o" live_stub.c
x86_64-w64-mingw32-as -o "$out/hook.o" hook.S
x86_64-w64-mingw32-ld --image-base=0 -T link.ld -o "$out/blob.o" "$out/hook.o" "$out/stub.o"
# Position-independent: no absolute address may need fixing up.
if x86_64-w64-mingw32-objdump -h "$out/blob.o" | grep -q '\.reloc'; then
    echo "base relocations left in the blob" >&2
    exit 1
fi
x86_64-w64-mingw32-objcopy -O binary -j .text "$out/blob.o" ../src/live_stub.bin
x86_64-w64-mingw32-nm "$out/blob.o" | grep -E ' (hook|run_requests|ctx|back)$'
