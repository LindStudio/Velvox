"""compare two runp.py outputs: python cmp.py old.txt new.txt"""
import sys
COLS = ['S4-12', 'Kern', 'Luft', 'L/K', 'Schwerp', 'unber%', '>8dB%', 'f/leise', 't/k', 'VokLuft', 'VokPres', 'nebenS', 'VokGR%', 'Rein%', 'retrig']


def parse(fn):
    out = {}
    for line in open(fn):
        p = line.split()
        if not p or p[0] in ('##', 'config', 'mix'):
            continue
        try:
            out[p[0]] = [float(x) for x in p[1:]]
        except ValueError:
            pass
    return out


a, b = parse(sys.argv[1]), parse(sys.argv[2])
show = ['Kern', 'Luft', 'Schwerp', 'unber%', '>8dB%', 'f/leise', 't/k', 'VokGR%', 'Rein%', 'retrig']
print('%-24s' % 'preset' + ''.join('%16s' % c for c in show))
for k in a:
    if k not in b:
        continue
    if len(a[k]) < len(COLS):
        print('%-24s mix Kern %.2f -> %.2f  VokLuft %.2f -> %.2f' % (k, a[k][1], b[k][1], a[k][2], b[k][2]))
        continue
    s = ''
    for c in show:
        i = COLS.index(c)
        s += '%16s' % ('%.2f>%.2f' % (a[k][i], b[k][i]))
    print('%-24s' % k + s)
