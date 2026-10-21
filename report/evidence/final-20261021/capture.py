#!/usr/bin/env python3
"""Capture real xterm windows displaying the recorded build and QEMU logs."""
from pathlib import Path
import hashlib
import json
import os
import shlex
import signal
import subprocess
import time

REPO = Path(__file__).resolve().parents[3]
WORK = REPO.parent / "tmp" / "report-completion"
XTERM = WORK / "xterm-root/usr/bin/xterm"
PREFIX = "report/evidence/final-20261021/"
CASES = [
    ("lab2-build-terminal.png", [
        ["cat", PREFIX + "buddy-build.log"],
        ["cat", PREFIX + "best-fit-build.log"]]),
    ("lab2-buddy-terminal.png", [
        ["sed", "-n", "/^memory management:/,/^SLUB CHECK BEGIN/{ /^SLUB CHECK BEGIN/!p; }",
         PREFIX + "buddy-qemu.log"]]),
    ("lab2-slub-terminal.png", [
        ["sed", "-n", "/^SLUB CHECK BEGIN/,/^SLUB CHECK ALL PASSED/p",
         PREFIX + "buddy-qemu.log"]]),
    ("lab2-best-fit-grade-terminal.png", [
        ["cat", PREFIX + "best-fit-grade.log"],
        ["tail", "-n", "5", PREFIX + "best-fit-qemu.log"]]),
]


def main():
    assert XTERM.is_file()
    images = REPO / "report/images"
    records = []
    for name, commands in CASES:
        state = WORK / name.removesuffix(".png")
        state.mkdir(exist_ok=True)
        ready, window = state / "ready", state / "window"
        ready.unlink(missing_ok=True)
        script = state / "display.py"
        script.write_text(
            "import os,time,subprocess,shlex\nfrom pathlib import Path\n"
            + "Path(" + repr(str(window)) + ").write_text(os.environ['WINDOWID'])\n"
            + "commands=" + repr(commands) + "\n"
            + "for args in commands:\n"
            + " print('$ '+shlex.join(args),flush=True)\n"
            + " subprocess.run(args,cwd=" + repr(str(REPO)) + ",check=True)\n"
            + " print(flush=True)\n"
            + "Path(" + repr(str(ready)) + ").write_text('ready')\n"
            + "time.sleep(25)\n")
        with (state / "xterm.log").open("w") as output:
            process = subprocess.Popen([
                str(XTERM), "-fa", "DejaVu Sans Mono", "-fs", "11",
                "-bg", "#101820", "-fg", "#dfe8f0", "-geometry", "140x48+20+20",
                "-title", "Lab2 " + name, "-e", "python3", str(script)],
                stdout=output, stderr=subprocess.STDOUT, start_new_session=True)
            try:
                deadline = time.monotonic() + 10
                while not ready.exists() and time.monotonic() < deadline:
                    assert process.poll() is None, (state / "xterm.log").read_text()
                    time.sleep(0.1)
                assert ready.exists()
                time.sleep(0.8)
                subprocess.run(["import", "-window", window.read_text(), str(images / name)],
                               check=True, timeout=10)
            finally:
                if process.poll() is None:
                    os.killpg(process.pid, signal.SIGTERM)
                    process.wait(timeout=5)
        records.append({"image": name, "capture_date": "2026-10-21",
                        "kind": "X11 xterm screenshot displaying actual saved command output",
                        "commands_displayed": commands,
                        "image_sha256": hashlib.sha256((images / name).read_bytes()).hexdigest()})
        print("Captured", name, flush=True)
    (Path(__file__).parent / "screenshots.json").write_text(
        json.dumps(records, ensure_ascii=False, indent=2) + "\n")


if __name__ == "__main__":
    main()
