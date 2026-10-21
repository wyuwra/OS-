#!/usr/bin/env python3
"""Build and capture the two Lab2 allocator configurations in temporary copies."""
from pathlib import Path
import hashlib
import json
import os
import shutil
import signal
import subprocess
import time

REPO = Path(__file__).resolve().parents[3]
SOURCE = REPO / "lab2_new"
OUTPUT = Path(__file__).resolve().parent
WORK = REPO.parent / "tmp" / "report-completion" / "runs"
QEMU = REPO.parent / "tmp" / "buddy-implementation" / "qemu-system-riscv64-4.1.1"


def hashes(root):
    return {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(root.rglob("*"))
            if p.is_file() and p.suffix in {".c", ".h", ".S", ".mk", ".ld"}
            or p.is_file() and p.name == "Makefile"}


def command(args, cwd, log):
    with log.open("w") as output:
        process = subprocess.run(args, cwd=cwd, stdout=output,
                                 stderr=subprocess.STDOUT, timeout=50)
    return {"command": args, "cwd": str(cwd), "exit_code": process.returncode}


def stop_group(process):
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        pass
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGKILL)
        process.wait()


def boot(tree, log, markers):
    args = ["make", "qemu", "SLUB_TEST=1", "QEMU=" + str(QEMU)]
    with log.open("w") as output:
        process = subprocess.Popen(args, cwd=tree, stdout=output,
                                   stderr=subprocess.STDOUT, start_new_session=True)
        try:
            deadline = time.monotonic() + 40
            while process.poll() is None and time.monotonic() < deadline:
                text = log.read_text(errors="replace")
                if all(m in text for m in markers) or "kernel panic" in text:
                    break
                time.sleep(0.1)
        finally:
            stop_group(process)
    text = log.read_text(errors="replace")
    result = {"command": args, "cwd": str(tree),
              "markers": {m: m in text for m in markers},
              "panic": "kernel panic" in text,
              "exit_code_after_capture_cleanup": process.returncode}
    assert all(result["markers"].values()) and not result["panic"], result
    return result


def grade(tree, log, temporary):
    args = ["make", "grade", "SLUB_TEST=1", "GRADE_TMP=" + str(temporary),
            "QEMU=" + str(QEMU)]
    with log.open("w") as output:
        process = subprocess.Popen(args, cwd=tree, stdout=output,
                                   stderr=subprocess.STDOUT, start_new_session=True)
        try:
            process.wait(timeout=50)
        finally:
            stop_group(process)
    return {"command": args, "cwd": str(tree), "exit_code": process.returncode}


def main():
    assert QEMU.is_file(), QEMU
    WORK.mkdir(parents=True, exist_ok=True)
    before = hashes(SOURCE)
    result = {"source_root": "lab2_new", "source_sha256": before,
              "compiler": subprocess.check_output(
                  ["riscv64-unknown-elf-gcc", "--version"], text=True).splitlines()[0],
              "qemu": subprocess.check_output([str(QEMU), "--version"], text=True).splitlines()[0],
              "cases": []}
    for manager in ["buddy", "best-fit"]:
        tree = WORK / manager
        assert not tree.exists(), "Use a fresh work directory for a new evidence capture"
        shutil.copytree(SOURCE, tree, ignore=shutil.ignore_patterns(
            "bin", "obj", ".gdb_history", ".qemu.out", ".gdb.in"))
        selector = "&buddy_pmm_manager"
        if manager == "best-fit":
            pmm = tree / "kern/mm/pmm.c"
            data = pmm.read_bytes()
            assert data.count(b"pmm_manager = &buddy_pmm_manager;") == 1
            pmm.write_bytes(data.replace(b"pmm_manager = &buddy_pmm_manager;",
                                        b"pmm_manager = &best_fit_pmm_manager;", 1))
            selector = "&best_fit_pmm_manager"
        build_log = OUTPUT / (manager + "-build.log")
        built = command(["make", "-j2", "SLUB_TEST=1"], tree, build_log)
        assert built["exit_code"] == 0, built
        markers = ["check_alloc_page() succeeded!", "SLUB CHECK ALL PASSED"]
        if manager == "buddy":
            markers += ["BUDDY CHECK ALL PASSED", "memory management: buddy_pmm_manager"]
        else:
            markers += ["memory management: best_fit_pmm_manager"]
        running = boot(tree, OUTPUT / (manager + "-qemu.log"), markers)
        grade_log = OUTPUT / (manager + "-grade.log")
        graded = grade(tree, grade_log, WORK / (manager + "-grade"))
        grade_text = grade_log.read_text(errors="replace")
        expected = "10/20" if manager == "buddy" else "20/20"
        assert "Total Score: " + expected in grade_text, grade_text
        assert graded["exit_code"] == (2 if manager == "buddy" else 0), graded
        result["cases"].append({"manager": manager, "selector": selector,
                                "build": built, "boot": running,
                                "grade": graded, "grade_score": expected})
        print(manager, "build and SLUB tests PASS; course grade", expected, flush=True)
    result["source_unchanged"] = hashes(SOURCE) == before
    assert result["source_unchanged"]
    result["completed"] = True
    (OUTPUT / "results.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n")


if __name__ == "__main__":
    main()
