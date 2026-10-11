import re, sys, os
# 工程根 = 本脚本所在目录的上级；dump.txt 由 recon.dll 注入游戏后生成到 dump\
_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DUMP = os.path.join(_ROOT, 'dump', 'dump.txt')
_cache = None

def blocks():
    global _cache
    if _cache is None:
        txt = open(DUMP, encoding='utf-8', errors='replace').read()
        _cache = re.split(r'\n(?=\[CLASS\])', txt)
    return _cache

def main():
    pats = sys.argv[1:]
    if not pats:
        print('usage: dumpq.py <class-name-regex> [...]'); return
    for pat in pats:
        rx = re.compile(pat)
        hits = 0
        for b in blocks():
            head = b.split('\n', 1)[0]
            if rx.search(head):
                print(b.rstrip())
                print()
                hits += 1
        if hits == 0:
            print('### no class matching %r\n' % pat)

main()
