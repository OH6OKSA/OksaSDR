# Turns "caption @TAG F" marks into cuts: windows whose F is kept stay, the rest go;
# a kept window starts at its own caption (the line after the mark).
import re, sys
src, dst, tag, keep = sys.argv[1], sys.argv[2], sys.argv[3], set(sys.argv[4:])
out, pending = [], False
for line in open(src, encoding='utf-8', errors='replace'):
    m = re.search(r'(shot-plan: at (\d+) caption )@' + tag + r' (\S+)', line)
    if m:
        kept = m.group(3) in keep
        out.append(m.group(1) + ('!' + tag + ' ' + m.group(3) if kept else '!cut') + '\n')
        pending = kept
        continue
    c = re.search(r'shot-plan: at (\d+) caption ', line)
    if pending and c:
        out.append(f'shot-plan: at {int(c.group(1)) - 1} caption !uncut\n')
        pending = False
    out.append(line)
open(dst, 'w').write(''.join(out))
