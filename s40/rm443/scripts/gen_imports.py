#!/usr/bin/env python3
"""Generate interworking veneers from this firmware's recovered export table.
Do not use libsystem's malloc/free: those are browser-private NW_Mem functions.
"""
import json
from pathlib import Path
P = Path(__file__).resolve().parents[1]
imports = json.loads((P/'docs/imports.json').read_text())
s = ['.syntax unified', '.arm', '.text']
for n,a in imports.items():
 s += [f'.section .text.{n},"ax",%progbits', '.balign 4', f'.global {n}',f'.hidden {n}', f'.type {n}, %function', f'{n}:', 'ldr ip, [pc, #0]', 'bx ip', f'.word 0x{a:08x}', f'.size {n}, .-{n}']
# ARMCC exports comparisons through CPU flags; Clang expects a boolean in r0.
for typ, target in [('d',0xF05387F8),('f',0xF0E1B17C)]:
 for op,cond in [('eq','eq'),('lt','cc'),('le','ls'),('ge','ge'),('gt','gt')]:
  n=f'__aeabi_{typ}cmp{op}'
  s += [f'.section .text.{n},"ax",%progbits','.balign 4',f'.global {n}',f'.hidden {n}',f'.type {n},%function',f'{n}:','push {r4,lr}',f'ldr ip, 1f','blx ip','mov r0,#0',f'mov{cond} r0,#1','pop {r4,pc}',f'1: .word 0x{target:08x}']
(P/'build/imports.S').write_text('\n'.join(s)+'\n')
(P/'build/imports.json').write_text(json.dumps(imports,indent=2)+'\n')
