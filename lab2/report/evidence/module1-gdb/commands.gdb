set pagination off
set confirm off
set architecture riscv:rv64
file bin/kernel
target remote 127.0.0.1:60649
break page_init
continue
printf "\n=== PHASE 1: page_init entry ===\n"
bt 3
p/x $pc
break *default_init_memmap
continue
printf "\n=== PHASE 2: before allocator takes ownership ===\n"
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
gdb.write('GDB PRE-HANDOFF CHECK PASSED\n')
gdb.write(json.dumps(facts, indent=2) + '\n')
end
delete breakpoints
tbreak *default_check
continue
printf "\n=== PHASE 3: allocator has registered free pages ===\n"
p free_area.nr_free
p *$base
p pages[0].flags
python
check(val('free_area.nr_free') == count, 'Free count mismatch')
check(val('$base->flags') == 2 and val('$base->property') == count,
      'First free block not registered')
check(val('pages[0].flags') == 1, 'Reserved prefix was released')
gdb.write('GDB POST-HANDOFF CHECK PASSED\n')
end
finish
printf "\n=== PHASE 4: original First-Fit self-check returned ===\n"
p free_area.nr_free
python
check(val('free_area.nr_free') == count, 'Self-check did not restore free count')
gdb.write('MODULE1 GDB CHECKS PASSED\n')
with open('gdb-facts.json', 'w') as f:
    json.dump(facts, f, indent=2)
end
detach
quit
