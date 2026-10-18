#!/usr/bin/env python3
# =============================================================================
#  toolchain-diff.py - GRIDEm's Resources/toolchain.json against GRIDEd's.
#  The two must say the same thing except which target is the host:
#  targets.*.host and targets.*.run, and the header comment. Exit 0 when they
#  agree, 1 (with the differences) when they do not.
#    python3 Tools/toolchain-diff.py [path/to/GRIDEd/GRIDE/toolchain.json]
# =============================================================================
import json, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
MINE = os.path.join(HERE, '..', 'Resources', 'toolchain.json')
THEIRS = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, '..', '..', 'GRIDE1', 'GRIDEd', 'GRIDE', 'toolchain.json')

def load(path):
    with open(path, encoding='utf-8') as f:
        text = re.sub(r'^\s*//.*$', '', f.read(), flags=re.M)
    doc = json.loads(text)
    for t in doc.get('targets', {}).values():
        if isinstance(t, dict):
            t.pop('host', None)
            t.pop('run', None)
    return doc

def walk(a, b, where, out):
    if type(a) != type(b):
        out.append('%s: %r vs %r' % (where, a, b)); return
    if isinstance(a, dict):
        for k in sorted(set(a) | set(b)):
            if k not in a: out.append('%s.%s: only in GRIDEd' % (where, k))
            elif k not in b: out.append('%s.%s: only in GRIDEm' % (where, k))
            else: walk(a[k], b[k], where + '.' + k, out)
    elif a != b:
        out.append('%s: %r vs %r' % (where, a, b))

diffs = []
walk(load(MINE), load(THEIRS), 'toolchain', diffs)
if diffs:
    print('GRIDEm and GRIDEd toolchain.json differ beyond the host fields:')
    for d in diffs: print('  ' + d)
    sys.exit(1)
print('toolchain.json: GRIDEm and GRIDEd agree (apart from the host)')
