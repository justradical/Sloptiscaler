#!/usr/bin/env python3
"""Rewrite OptiScaler's Source.def for the ARM64EC/mingw link.

Source.def names its export targets undecorated (e.g. "_D3D12CreateDeviceExport").
MSVC's linker resolves an undecorated name to the unique matching decorated
symbol; the mingw driver does not. This maps each target to the real mangled
symbol found in the compiled objects.

Usage: gen_def.py <Source.def> <symbols.txt> <out.def>
"""
import re
import sys

src_def, syms_file, out_def = sys.argv[1], sys.argv[2], sys.argv[3]

# Index mangled C++ names by the identifier they encode: _Z<len><name>...
index = {}
for line in open(syms_file):
    s = line.strip()
    m = re.match(r'^_Z(\d+)(.+)$', s)
    if m:
        index.setdefault(m.group(2)[:int(m.group(1))], []).append(s)

out, missing = ['EXPORTS'], []
for line in open(src_def, encoding='utf-8'):
    line = line.strip()
    if not line or line.upper() == 'EXPORTS':
        continue
    m = re.match(r'^(\S+)\s*=\s*(\S+)\s*(@\d+)?\s*(PRIVATE)?', line)
    if not m:
        continue
    name, target, ordinal, private = m.group(1), m.group(2), m.group(3) or '', m.group(4) or ''
    cands = index.get(target, [])
    if cands:
        out.append('%s = %s %s %s' % (name, sorted(cands, key=len)[0], ordinal, private))
    else:
        missing.append((name, target))

open(out_def, 'w').write('\n'.join(out) + '\n')
print('   %d exports mapped, %d unresolved' % (len(out) - 1, len(missing)))
for name, target in missing[:10]:
    print('   unresolved: %s -> %s' % (name, target))
