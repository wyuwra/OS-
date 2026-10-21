#!/usr/bin/env python3
"""Render readable PNG evidence from saved logs, not terminal screenshots.

Run with Windows Python + Pillow: python report/evidence/render_evidence.py
Input snippets are preserved verbatim except CRLF normalization and line wrapping.
"""
import hashlib
import json
from pathlib import Path
import textwrap

from PIL import Image, ImageDraw, ImageFont


REPORT = Path(__file__).resolve().parent.parent
OUT = REPORT / 'images'
FONT = Path('C:/Windows/Fonts')
MONO = ImageFont.truetype(str(FONT / 'consola.ttf'), 23)
TITLE = ImageFont.truetype(str(FONT / 'msyh.ttc'), 34)
LABEL = ImageFont.truetype(str(FONT / 'msyh.ttc'), 21)
manifest = []


def render(filename, title, source, body, kind='verbatim log excerpt'):
    lines = []
    for line in body.splitlines():
        lines.extend(textwrap.wrap(line.expandtabs(4), width=121,
                                   subsequent_indent='  ', replace_whitespace=False,
                                   drop_whitespace=False) or [''])
    width, height = 1770, 218 + len(lines) * 32
    canvas = Image.new('RGB', (width, height), '#f2f5f8')
    draw = ImageDraw.Draw(canvas)
    draw.rectangle((0, 0, 12, height), fill='#137c72')
    draw.text((44, 27), title, font=TITLE, fill='#183347')
    draw.text((46, 86), '真实日志阅读视图 · 非终端窗口截图 · 自动换行', font=LABEL, fill='#526273')
    draw.text((46, 126), 'Source: ' + str(source.relative_to(REPORT)), font=MONO, fill='#526273')
    draw.rounded_rectangle((32, 173, width - 32, height - 25), radius=12, fill='#102437')
    y = 189
    for line in lines:
        color = '#7de1bd' if ('PASSED' in line or 'succeeded!' in line or 'PASS' in line) else '#e5edf4'
        draw.text((52, y), line, font=MONO, fill=color)
        y += 32
    canvas.save(OUT / filename)
    manifest.append({'image': filename, 'kind': kind,
                     'source': str(source.relative_to(REPORT)),
                     'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
                     'display': 'CRLF normalized; long lines wrapped, no terminal UI simulation'})


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    serial = REPORT / 'evidence/module1/ram128-qemu.log'
    text = serial.read_text(encoding='utf-8')
    snippet = text[text.index('DTB Init'):text.index('qemu-system-riscv64: terminating')].rstrip()
    render('module1-boot.png', '模块一 / 128 MiB 启动与初始化检查', serial, snippet)

    debug = REPORT / 'evidence/module1-gdb/gdb.log'
    text = debug.read_text(encoding='utf-8')
    snippet = text[text.index('=== PHASE 2:'):text.index('Temporary breakpoint 3 at')].rstrip()
    render('module1-gdb-before.png', 'GDB / 分配器接管前', debug, snippet)
    snippet = text[text.index('=== PHASE 3:'):text.index('[Inferior 1')].rstrip()
    render('module1-gdb-after.png', 'GDB / 空闲区登记后与原自检返回', debug, snippet)

    results = REPORT / 'evidence/module1/results.json'
    data = json.loads(results.read_text(encoding='utf-8'))
    lines = ['status: ' + data['status'], 'run_id: ' + data['run_id'],
             'source_unchanged_by_verification: ' + str(data['source_unchanged_by_verification']),
             '', 'CASE                     RAM/MiB  RESULT', '-' * 49]
    for case in data['cases']:
        lines.append(f"{case['case']:<25}{case['memory_mib']:<9}{case['result']}")
    lines += ['', 'Positive cases: explicit completion + handoff/layout + First-Fit checks.',
              'Negative cases: expected panic + no allocator handoff.',
              '256 MiB: official uncapped expectation SKIP; added capped-layout check PASS.',
              '', 'Scope: temporary First-Fit build; unfinished Best-Fit excluded.',
              'This is module-one evidence, not a full lab2 grade result.']
    render('module1-case-summary.png', '模块一 / 九组检查结果摘要', results, '\n'.join(lines),
           kind='structured rendering of actual results.json')
    (OUT / 'image-sources.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n',
                                          encoding='utf-8')
    print('Rendered', len(manifest), 'evidence images from actual saved results.')


if __name__ == '__main__':
    main()
