"""Extract verbatim log ranges for displaying and taking editor screenshots."""
from pathlib import Path

folder = Path(__file__).resolve().parent
gdb_log = (folder / "gdb-entry.txt").read_text(encoding="utf-8")
elf_log = (folder / "elf-analysis.txt").read_text(encoding="utf-8")


def extract(text, start, stop=None):
    begin = text.index(start)
    finish = text.index(stop, begin) if stop else len(text)
    return text[begin:finish].rstrip() + "\n"


views = {
    "view-stack-tail.txt": extract(gdb_log, "=== A:", "Dump of assembler code"),
    "view-frame-call.txt": extract(gdb_log, "=== D:", "=== G:"),
    "view-loop-bss.txt": extract(gdb_log, "=== G:", "[Inferior"),
    "view-relocations.txt": extract(
        elf_log,
        "$ riscv64-unknown-elf-objdump -dr",
        "$ riscv64-unknown-elf-objdump -d bin/kernel --start-address=0x80200000",
    ),
    "view-linked-entry.txt": extract(
        elf_log, "0000000080200000 <kern_entry>:", "000000008020000a <kern_init>:"
    ) + extract(elf_log, "00000000802004be <memset>:"),
}
for filename, content in views.items():
    (folder / filename).write_text(content, encoding="utf-8", newline="\n")
    print(f"{filename}: {len(content.splitlines())} verbatim log lines")
