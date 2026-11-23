import sys
sys.stdout.reconfigure(encoding='utf-8')
from presets import P
from gen_cpp import DESC
CATS = [('basics', 'Gesang-Grundlagen', 'Vocal Basics'), ('male', 'Männerstimmen', 'Male Vocals'), ('female', 'Frauenstimmen', 'Female Vocals'),
        ('genre', 'Genres', 'Genre'), ('special', 'Spezialfälle', 'Special Purpose')]
lang = sys.argv[1]
i = 0 if lang == 'de' else 1
print('<table>')
for c, de, en in CATS:
    print('  <tr><th colspan="2">%s</th></tr>' % (de if i == 0 else en))
    for p in P:
        if p[1] == c:
            print('  <tr><td>%s</td><td>%s</td></tr>' % (p[0], DESC[p[0]][i]))
print('</table>')
