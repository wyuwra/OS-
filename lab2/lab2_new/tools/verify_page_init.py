#!/usr/bin/env python3
"""Build and verify module one in temporary RISC-V kernels (run inside WSL).

The student's source and official tests stay unchanged. Only the temporary
Makefile excludes the unfinished best_fit_pmm.c; the original module-one test
and an additional full-page layout check run with the existing First-Fit.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
import uuid


SUCCESS_MARKER = "MODULE1 VERIFICATION COMPLETE"


def save_summary(log_dir, summary):
    temporary = log_dir / "results.json.tmp"
    temporary.write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n",
                         encoding="utf-8")
    temporary.replace(log_dir / "results.json")


def source_hashes(root):
    return {
        str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
        for p in root.rglob("*")
        if p.is_file() and not {"obj", "bin", "__pycache__"}.intersection(
            p.relative_to(root).parts)
    }


def replace_once(path, old, new):
    text = path.read_text(encoding="utf-8")
    if text.count(old) != 1:
        raise RuntimeError("Unexpected source layout: " + str(path))
    path.write_text(text.replace(old, new, 1), encoding="utf-8")


def run_case(root, case, log_dir, qemu, env):
    name, memory, fault, expected = case
    if fault:
        dtb = root / "kern/driver/dtb.c"
        if fault == "zero":
            replace_once(dtb, "return memory_size;", "return 0;")
        elif fault == "unaligned-end":
            replace_once(dtb, "return memory_size;", "return memory_size - 1;")
        elif fault == "overflow":
            replace_once(dtb, "return memory_base;", "return (uint64_t)-4096;")
            replace_once(dtb, "return memory_size;", "return 8192;")
        elif fault == "too-small":
            replace_once(dtb, "return memory_size;", "return 0x200000;")
        elif fault == "metadata-short":
            replace_once(dtb, "#include <dtb.h>",
                         "#include <dtb.h>\n#include <pmm.h>")
            replace_once(dtb, "return memory_size;", """
    extern char end[];
    return ROUNDUP(PADDR(end), PGSIZE) + PGSIZE - DRAM_BASE;""")
        elif fault == "no-free":
            replace_once(dtb, "#include <dtb.h>",
                         "#include <dtb.h>\n#include <pmm.h>")
            # The first page boundary able to hold all descriptors leaves no
            # complete free page. Derive it from this test kernel's actual end.
            replace_once(dtb, "return memory_size;", """
    extern char end[];
    uintptr_t start = ROUNDUP(PADDR(end), PGSIZE);
    for (uintptr_t stop = start; stop < PHYSICAL_MEMORY_END; stop += PGSIZE) {
        size_t descriptors = PPN(stop) - PPN(DRAM_BASE);
        if (start + descriptors * sizeof(struct Page) <= stop) {
            return stop - DRAM_BASE;
        }
    }
    return 0;""")
        else:
            raise RuntimeError("Unknown fault: " + fault)

    build = subprocess.run(["make", "-j2"], cwd=root, env=env,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           text=True, timeout=60)
    (log_dir / (name + "-build.log")).write_text(build.stdout, encoding="utf-8")
    if build.returncode:
        raise RuntimeError(name + " build failed:\n" + build.stdout[-5000:])

    # Stream inside Linux /tmp: concurrent DrvFS reads can fail with ENODATA.
    output_path = root / "verification-qemu.log"
    saved_output = log_dir / (name + "-qemu.log")
    command = [qemu, "-machine", "virt", "-m", str(memory), "-nographic",
               "-monitor", "none", "-bios", "default", "-device",
               "loader,file=bin/ucore.img,addr=0x80200000"]
    with output_path.open("w", encoding="utf-8") as stream:
        process = subprocess.Popen(command, cwd=root, env=env,
                                   stdout=stream, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 15
            while time.monotonic() < deadline:
                output = output_path.read_text(encoding="utf-8", errors="replace")
                if expected in output or process.poll() is not None:
                    break
                time.sleep(0.05)
        finally:
            # Stop only the QEMU process started for this case.
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=3)

    output = output_path.read_text(encoding="utf-8", errors="replace")
    saved_output.write_text(output, encoding="utf-8")
    if expected not in output:
        raise RuntimeError(name + " did not reach its expected marker:\n" + output)
    if expected == SUCCESS_MARKER:
        required = ["check_alloc_page() succeeded!",
                    "MODULE1 FULL-LAYOUT CHECK PASSED",
                    "MODULE1 HANDOFF CONTRACT PASSED"]
        required.append("[Test 1] Page Init: PASS" if memory <= 128 else
                        "[Test 1] Page Init: SKIP (uncapped npage expectation)")
        if "kernel panic" in output or not all(x in output for x in required):
            raise RuntimeError(name + " layout/self-check failure:\n" + output)
    elif "MODULE1 INIT_MEMMAP ENTERED" in output:
        raise RuntimeError(name + " handed invalid memory to the allocator:\n" + output)
    print("PASS:", name, "=>", expected, flush=True)
    return {"case": name, "memory_mib": memory, "fault": fault,
            "expected_marker": expected, "result": "PASS",
            "build_log": name + "-build.log", "qemu_log": saved_output.name}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--log-dir", type=Path, required=True)
    parser.add_argument("--case", action="append", default=[],
                        help="Run only the named case (repeatable); default: all cases")
    args = parser.parse_args()
    source = Path(__file__).resolve().parents[1]
    log_dir = args.log_dir.resolve()
    if log_dir == source or source in log_dir.parents:
        parser.error("Write verification logs outside the source tree")
    log_dir.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env["PATH"] = ":".join([
        str(Path.home() / ".local/opt/riscv/bin"),
        str(Path.home() / ".local/opt/qemu-4.1.1/bin"), env.get("PATH", "")])
    qemu = shutil.which("qemu-system-riscv64", path=env["PATH"])
    compiler = shutil.which("riscv64-unknown-elf-gcc", path=env["PATH"])
    if not qemu or not compiler:
        parser.error("RISC-V GCC and QEMU must be available")
    before = source_hashes(source)
    versions = {
        "compiler": subprocess.check_output([compiler, "--version"], text=True).splitlines()[0],
        "qemu": subprocess.check_output([qemu, "--version"], text=True).splitlines()[0],
    }
    cases = [
        ("ram128", 128, None, SUCCESS_MARKER),
        ("ram64", 64, None, SUCCESS_MARKER),
        ("ram64-unaligned", 64, "unaligned-end", SUCCESS_MARKER),
        ("ram256-capped", 256, None, SUCCESS_MARKER),
        ("invalid-zero", 128, "zero", "page_init: invalid memory information from DTB"),
        ("invalid-overflow", 128, "overflow", "page_init: physical memory range overflows"),
        ("insufficient-ram", 128, "too-small", "page_init: RAM cannot contain the kernel and page descriptors"),
        ("insufficient-metadata", 128, "metadata-short", "page_init: insufficient RAM for page descriptors"),
        ("no-free-pages", 128, "no-free", "page_init: no free physical pages"),
    ]
    if args.case:
        unknown = set(args.case) - {case[0] for case in cases}
        if unknown:
            parser.error("Unknown cases: " + ", ".join(sorted(unknown)))
        cases = [case for case in cases if case[0] in args.case]
    results = []
    summary = {
        "run_id": uuid.uuid4().hex, "status": "RUNNING", "versions": versions,
        "source_sha256": before, "cases": results,
        "isolation": "Temporary copy excludes unfinished Best-Fit; original First-Fit and official tests unchanged",
    }
    # Invalidate any old successful summary before this run starts building.
    save_summary(log_dir, summary)
    try:
        run_suite(source, cases, results, log_dir, qemu, env, summary)
        if source_hashes(source) != before:
            raise RuntimeError("Verification unexpectedly modified original sources")
    except BaseException as error:
        summary.update(status="FAIL", error=str(error),
                       source_unchanged_by_verification=source_hashes(source) == before)
        save_summary(log_dir, summary)
        raise
    summary.update(status="PASS", active_case=None,
                   source_unchanged_by_verification=True)
    save_summary(log_dir, summary)
    print("ALL MODULE1 CHECKS PASSED; original sources unchanged by verification.")


def run_suite(source, cases, results, log_dir, qemu, env, summary):
    with tempfile.TemporaryDirectory(prefix="lab2-page-init-") as temporary:
        template = Path(temporary) / "template"
        shutil.copytree(source, template,
                        ignore=shutil.ignore_patterns("obj", "bin", "__pycache__"))
        makefile = template / "Makefile"
        text = makefile.read_text(encoding="utf-8")
        needle = "$(call listf_cc,$(KSRCDIR))"
        if text.count(needle) != 2:
            raise RuntimeError("Unexpected Makefile source-list layout")
        text = text.replace(needle,
            "$(filter-out kern/mm/best_fit_pmm.c," + needle + ")")
        text = text.replace("KCFLAGS\t\t+=", "KINCLUDE += kern/test\nKSRCDIR += kern/test\n\nKCFLAGS\t\t+=", 1)
        makefile.write_text(text, encoding="utf-8")
        test_dir = template / "kern/test"
        test_dir.mkdir()
        for name in ("test_lab2.c", "test_lab2.h", "check_page_init.c"):
            shutil.copyfile(template / "tools" / name, test_dir / name)
        pmm = template / "kern/mm/pmm.c"
        replace_once(pmm, "#include <pmm.h>",
            "#include <pmm.h>\n#include <test_lab2.h>\n"
            "void check_page_init_layout(void);\n"
            "void check_page_init_handoff(struct Page *, size_t);")
        replace_once(pmm, "    pmm_manager->init_memmap(base, n);",
            "    check_page_init_handoff(base, n);\n"
            "    pmm_manager->init_memmap(base, n);")
        replace_once(pmm, "    check_alloc_page();", """    check_alloc_page();
    // The capped case needs an independent oracle: official Test 1 assumes no cap.
    if (get_memory_base() + get_memory_size() <= PHYSICAL_MEMORY_END) {
        test_page_init();
    } else {
        cprintf("[Test 1] Page Init: SKIP (uncapped npage expectation)\\n");
    }
    check_page_init_layout();""")
        replace_once(template / "kern/init/init.c",
            "    pmm_init();  // init physical memory management",
            "    pmm_init();  // init physical memory management\n"
            '    cprintf("' + SUCCESS_MARKER + '\\n");')

        # The 256 MiB oracle intentionally does not demand the official PASS.
        # Its full-page interval checks independently validate the 128 MiB cap.
        for case in cases:
            summary["active_case"] = case[0]
            save_summary(log_dir, summary)
            root = Path(temporary) / case[0]
            shutil.copytree(template, root)
            results.append(run_case(root, case, log_dir, qemu, env))
            save_summary(log_dir, summary)


if __name__ == "__main__":
    main()
