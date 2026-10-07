#!/usr/bin/env bash
# Run in Ubuntu WSL: bash report/evidence/verify-exercise1.sh
set -euo pipefail
task_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
task_evidence="$task_root/report/evidence"
export PATH="$HOME/.local/opt/qemu-4.1.1/bin:$HOME/.local/opt/riscv/bin:$PATH"
cd "$task_root/code"
{
    TZ=Asia/Shanghai date --iso-8601=seconds
    uname -srmo
    riscv64-unknown-elf-gcc --version
    qemu-system-riscv64 --version
    gdb-multiarch --version
    make -j2
    sha256sum kern/init/entry.S kern/init/init.c tools/kernel.ld bin/kernel bin/ucore.img
} > "$task_evidence/environment-build.txt" 2>&1
{
    echo '$ riscv64-unknown-elf-nm -n bin/kernel'
    riscv64-unknown-elf-nm -n bin/kernel
    echo '$ riscv64-unknown-elf-readelf -h -SW bin/kernel'
    riscv64-unknown-elf-readelf -h -SW bin/kernel
    echo '$ riscv64-unknown-elf-objdump -dr obj/kern/init/entry.o'
    riscv64-unknown-elf-objdump -dr obj/kern/init/entry.o
    echo '$ riscv64-unknown-elf-objdump -d bin/kernel --start-address=0x80200000 --stop-address=0x8020003c'
    riscv64-unknown-elf-objdump -d bin/kernel --start-address=0x80200000 --stop-address=0x8020003c
    echo '$ riscv64-unknown-elf-objdump -d bin/kernel --start-address=0x802004be --stop-address=0x802004d0'
    riscv64-unknown-elf-objdump -d bin/kernel --start-address=0x802004be --stop-address=0x802004d0
} > "$task_evidence/elf-analysis.txt" 2>&1
qemu-system-riscv64 -machine virt -nographic -bios default \
    -device loader,file=bin/ucore.img,addr=0x80200000 \
    -gdb tcp:127.0.0.1:1235 -S > "$task_evidence/qemu-output.txt" 2>&1 &
task_qemu_pid=$!
cleanup() {
    kill "$task_qemu_pid" 2>/dev/null || true
    wait "$task_qemu_pid" 2>/dev/null || true
}
trap cleanup EXIT
sleep 1
if ! kill -0 "$task_qemu_pid" 2>/dev/null; then
    cat "$task_evidence/qemu-output.txt"
    exit 1
fi
timeout --kill-after=2s 30s gdb-multiarch -q -nx -batch bin/kernel \
    -x "$task_evidence/exercise1.gdb" > "$task_evidence/gdb-entry.txt" 2>&1
cat "$task_evidence/gdb-entry.txt"
