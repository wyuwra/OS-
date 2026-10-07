set pagination off
set confirm off
set print pretty off
set architecture riscv:rv64
target remote 127.0.0.1:1235
break *kern_entry
continue

echo \n=== A: kernel entry, before stack initialization ===\n
info registers pc sp ra
p/x &bootstack
p/x &bootstacktop
p/d (unsigned long)&bootstacktop - (unsigned long)&bootstack
x/3i $pc

stepi
stepi
echo \n=== B: after la sp, bootstacktop ===\n
info registers pc sp ra
python
assert int(gdb.parse_and_eval('$sp')) == int(gdb.parse_and_eval('&bootstacktop'))
assert int(gdb.parse_and_eval('$sp')) % 16 == 0
entry_ra = int(gdb.parse_and_eval('$ra'))
print('PASS: sp equals bootstacktop and is 16-byte aligned')
end

stepi
echo \n=== C: after tail kern_init ===\n
info registers pc sp ra
python
assert int(gdb.parse_and_eval('$pc')) == int(gdb.parse_and_eval('&kern_init'))
assert int(gdb.parse_and_eval('$ra')) == entry_ra
print('PASS: pc equals kern_init; tail did not change ra')
end
disassemble /r kern_init

# These addresses match the recorded GCC 8.3.0 build. Recheck disassembly
# before using this script with a different compiler or modified source.
tbreak *0x80200022
continue
echo \n=== D: C prologue, before calling memset ===\n
info registers pc sp ra a0 a1 a2
x/gx $sp+8
python
assert int(gdb.parse_and_eval('$sp')) == int(gdb.parse_and_eval('&bootstacktop')) - 16
assert int(gdb.parse_and_eval('*(unsigned long *)($sp + 8)')) == entry_ra
assert int(gdb.parse_and_eval('$a2')) == 0
print('PASS: 16-byte stack frame; incoming ra saved at sp+8; memset length is 0')
end

stepi
echo \n=== E: jal to memset creates a new return address ===\n
info registers pc sp ra
python
assert int(gdb.parse_and_eval('$pc')) == int(gdb.parse_and_eval('&memset'))
assert int(gdb.parse_and_eval('$ra')) == 0x80200026
print('PASS: jal sets ra to 0x80200026')
end
stepi
echo \n=== E2: zero-length memset branches directly to ret ===\n
info registers pc a2
python
assert int(gdb.parse_and_eval('$pc')) == 0x802004ce
assert int(gdb.parse_and_eval('$a2')) == 0
print('PASS: beqz a2 bypassed the store loop; no memory store executed')
end
stepi
echo \n=== F: returned from memset ===\n
info registers pc sp ra
python
assert int(gdb.parse_and_eval('$pc')) == 0x80200026
print('PASS: ret returns to 0x80200026')
end

tbreak *0x8020003a
continue
echo \n=== G: terminal loop after cprintf ===\n
info registers pc sp ra
x/i $pc
stepi
stepi
python
assert int(gdb.parse_and_eval('$pc')) == 0x8020003a
print('PASS: two single steps keep pc at the terminal loop')
end
p/x &edata
p/x &end
p/d (unsigned long)&end - (unsigned long)&edata
echo \nALL EXERCISE 1 CHECKS PASSED\n
detach
quit
