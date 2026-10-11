"""Print the Auto Assembler scripts inside a Cheat Engine table (.ct).

usage: python ct_script.py <table.ct> [entry-name-regex]
"""
import xml.etree.ElementTree as ET
import sys, re, io

sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')

if len(sys.argv) < 2:
    print(__doc__)
    sys.exit(2)
P = sys.argv[1]
txt = open(P, 'rb').read().decode('utf-8')
root = ET.fromstring(txt)

entries = []
def walk(node, depth=0):
    for ch in node:
        if ch.tag == 'CheatEntry':
            desc = (ch.findtext('Description') or '').strip()
            entries.append((depth, desc, ch))
            walk(ch, depth + 1)
        elif len(ch):
            walk(ch, depth)
walk(root)

pat = sys.argv[1] if len(sys.argv) > 1 else None
for depth, desc, node in entries:
    if pat and not re.search(pat, desc):
        continue
    script = node.findtext('AssemblerScript') or ''
    print('=' * 100)
    print('%s%s' % ('  ' * depth, desc))
    if script:
        print('-' * 100)
        print(script)
    print()
