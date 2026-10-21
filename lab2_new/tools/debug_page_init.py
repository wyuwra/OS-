#!/usr/bin/env python3
"""Verify page_init with real batch GDB/QEMU, in a temporary WSL build.

Only the temporary Makefile excludes the unfinished Best-Fit implementation.
No kernel C/assembly or official tests are changed. Logs stay outside source.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import tempfile
import time

# Import the shared hash helper without creating build clutter in the source tree.
sys.dont_write_bytecode = True
from verify_page_init import source_hashes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--log-dir', required=True, type=Path)
    args = parser.parse_args()
    source = Path(__file__).resolve().parents[1]
    logs = args.log_dir.resolve()
    if logs == source or source in logs.parents:
        parser.error('Write logs outside the source tree')
    logs.mkdir(parents=True, exist_ok=True)
    before = source_hashes(source)
    summary = {'status': 'RUNNING', 'source_sha256': before,
               'scope': '128 MiB, original C/assembly, First-Fit; temporary Makefile excludes Best-Fit'}
    result_file = logs / 'results.json'
    result_file.write_text(json.dumps(summary, indent=2) + '\n')
    env = os.environ.copy()
    env['PATH'] = ':'.join([str(Path.home() / '.local/opt/riscv/bin'),
                            str(Path.home() / '.local/opt/qemu-4.1.1/bin'), env.get('PATH', '')])
    try:
        qemu = shutil.which('qemu-system-riscv64', path=env['PATH'])
        gdb = shutil.which('gdb-multiarch', path=env['PATH'])
        if not qemu or not gdb:
            raise RuntimeError('QEMU and gdb-multiarch are required')
        summary['gdb_version'] = subprocess.check_output([gdb, '--version'], text=True).splitlines()[0]
        with tempfile.TemporaryDirectory(prefix='lab2-gdb-') as temp:
            root = Path(temp) / 'kernel'
            shutil.copytree(source, root, ignore=shutil.ignore_patterns('obj', 'bin', '__pycache__'))
            makefile = root / 'Makefile'
            text = makefile.read_text()
            old = '$(call listf_cc,$(KSRCDIR))'
            if text.count(old) != 2:
                raise RuntimeError('Unexpected Makefile source list')
            makefile.write_text(text.replace(old, '$(filter-out kern/mm/best_fit_pmm.c,' + old + ')'))
            build = subprocess.run(['make', '-j2'], cwd=root, env=env, text=True,
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
            (logs / 'build.log').write_text(build.stdout)
            if build.returncode:
                raise RuntimeError('Build failed; see build.log')
            with socket.socket() as sock:
                sock.bind(('127.0.0.1', 0))
                port = sock.getsockname()[1]
            commands = '''set pagination off
set confirm off
set architecture riscv:rv64
file bin/kernel
target remote 127.0.0.1:PORT
break page_init
continue
printf "\\n=== PHASE 1: page_init entry ===\\n"
bt 3
p/x $pc
break *default_init_memmap
continue
printf "\\n=== PHASE 2: before allocator takes ownership ===\\n"
set $base = (struct Page *)$a0
set $count = (unsigned long)$a1
p/x va_pa_offset
p/x nbase
p/x npage
p/x pages
p sizeof(struct Page)
p/x &end
p/x $base
p/d $count
p *$base
p pages[0]
python
import gdb, json
def val(expr):
    return int(gdb.parse_and_eval(expr))
def check(condition, message):
    if not condition:
        raise gdb.GdbError(message)
offset = val('va_pa_offset')
nb, np = val('nbase'), val('npage')
pages_va, sz = val('pages'), val('sizeof(struct Page)')
base, count = val('$base'), val('$count')
idx = (base - pages_va) // sz
free_pa = (nb + idx) * 4096
metadata_end = pages_va - offset + (np - nb) * sz
expected_begin = (metadata_end + 4095) & ~4095
check(offset == 0xffffffff40000000, 'Wrong direct-map offset')
check(nb == 0x80000 and np == 0x88000, 'Wrong 128 MiB page bounds')
check(base >= pages_va and (base - pages_va) % sz == 0, 'Invalid Page pointer')
check(count > 0 and idx + count == np - nb, 'Wrong handoff size')
check(free_pa == expected_begin, 'Wrong first free physical page')
for expression in ['pages[0]', 'pages[%d]' % (idx - 1), '*$base', 'pages[%d]' % (np - nb - 1)]:
    p = gdb.parse_and_eval(expression)
    check(int(p['flags']) == 1 and int(p['ref']) == 0 and int(p['property']) == 0,
          'Descriptor not initially reserved/zero: ' + expression)
    link = p['page_link']
    check(int(link['prev']) == int(link.address) and int(link['next']) == int(link.address),
          'Descriptor list is not initialized: ' + expression)
facts = dict(nbase=nb, npage=np, descriptor_count=np-nb, descriptor_size=sz,
             pages_va=hex(pages_va), pages_pa=hex(pages_va-offset),
             kernel_end_va=hex(val('&end')), free_begin_pa=hex(free_pa),
             managed_end_pa=hex(np*4096), free_pages=count)
gdb.write('GDB PRE-HANDOFF CHECK PASSED\\n')
gdb.write(json.dumps(facts, indent=2) + '\\n')
end
delete breakpoints
tbreak *default_check
continue
printf "\\n=== PHASE 3: allocator has registered free pages ===\\n"
p free_area.nr_free
p *$base
p pages[0].flags
python
check(val('free_area.nr_free') == count, 'Free count mismatch')
check(val('$base->flags') == 2 and val('$base->property') == count,
      'First free block not registered')
check(val('pages[0].flags') == 1, 'Reserved prefix was released')
gdb.write('GDB POST-HANDOFF CHECK PASSED\\n')
end
finish
printf "\\n=== PHASE 4: original First-Fit self-check returned ===\\n"
p free_area.nr_free
python
check(val('free_area.nr_free') == count, 'Self-check did not restore free count')
gdb.write('MODULE1 GDB CHECKS PASSED\\n')
with open('gdb-facts.json', 'w') as f:
    json.dump(facts, f, indent=2)
end
detach
quit
'''.replace('PORT', str(port))
            script = root / 'commands.gdb'
            script.write_text(commands)
            (logs / 'commands.gdb').write_text(commands)
            serial = root / 'serial.log'
            with serial.open('w') as stream:
                process = subprocess.Popen([qemu, '-machine', 'virt', '-m', '128', '-nographic',
                                            '-monitor', 'none', '-bios', 'default', '-S',
                                            '-gdb', 'tcp:127.0.0.1:' + str(port), '-device',
                                            'loader,file=bin/ucore.img,addr=0x80200000'],
                                           cwd=root, env=env, stdout=stream, stderr=subprocess.STDOUT)
                try:
                    time.sleep(0.3)
                    debug = subprocess.run([gdb, '-q', '-nx', '-batch', '-x', str(script)],
                                           cwd=root, env=env, text=True, stdout=subprocess.PIPE,
                                           stderr=subprocess.STDOUT, timeout=35)
                    (logs / 'gdb.log').write_text(debug.stdout)
                    if debug.returncode or 'MODULE1 GDB CHECKS PASSED' not in debug.stdout:
                        raise RuntimeError('GDB verification failed; see gdb.log')
                    deadline = time.monotonic() + 5
                    while time.monotonic() < deadline:
                        if 'satp physical address:' in serial.read_text(errors='replace'):
                            break
                        time.sleep(0.05)
                finally:
                    if process.poll() is None:
                        process.terminate()
                        try:
                            process.wait(timeout=3)
                        except subprocess.TimeoutExpired:
                            process.kill()
                            process.wait(timeout=3)
                    (logs / 'qemu.log').write_text(serial.read_text(errors='replace'))
            if 'check_alloc_page() succeeded!' not in serial.read_text(errors='replace'):
                raise RuntimeError('Kernel did not continue successfully after GDB detach')
            summary['facts'] = json.loads((root / 'gdb-facts.json').read_text())
        if before != source_hashes(source):
            raise RuntimeError('Source changed during verification')
        summary['status'] = 'PASS'
        summary['source_unchanged'] = True
        print('PASS: actual GDB pre-handoff, post-handoff and original allocator self-check')
        print(json.dumps(summary['facts'], indent=2))
    except Exception as exc:
        summary['status'] = 'FAIL'
        summary['error'] = str(exc)
        raise
    finally:
        result_file.write_text(json.dumps(summary, indent=2) + '\n')


if __name__ == '__main__':
    main()
