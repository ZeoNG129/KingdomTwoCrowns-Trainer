"""Dump the entry tree of a Cheat Engine table (.ct).

usage: python ct_dump.py <table.ct> [name-filter-regex]
"""
import xml.etree.ElementTree as ET
import sys, re

if len(sys.argv) < 2:
    print(__doc__)
    sys.exit(2)
P = sys.argv[1]
raw = open(P, 'rb').read()
for enc in ('utf-8', 'gbk', 'gb18030', 'utf-16'):
    try:
        txt = raw.decode(enc)
        print('# decoded as', enc)
        break
    except Exception:
        continue
root = ET.fromstring(txt)

entries = []
def walk(node, depth=0):
    for ch in node:
        if ch.tag == 'CheatEntry':
            desc = (ch.findtext('Description') or '').strip()
            vt = (ch.findtext('VariableType') or '').strip()
            entries.append((depth, vt, desc, ch))
            walk(ch, depth + 1)
        elif len(ch):
            walk(ch, depth)
walk(root)

print('total entries:', len(entries))
print()
kw = sys.argv[1] if len(sys.argv) > 1 else None
for depth, vt, desc, node in entries:
    if kw and not re.search(kw, desc, re.I):
        continue
    print('%s%s%s' % ('  ' * depth, ('[%s] ' % vt) if vt else '', desc))
