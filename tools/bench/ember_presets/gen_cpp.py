"""Prints the C++ preset list for EmberPresets.cpp from presets.py (single source for bench and plugin)."""
from presets import P

DESC = {
    'Gentle Vocal': ('Leichte, unauffällige Absenkung für Stimmen mit nur wenig scharfen S.',
                     'Light, unobtrusive reduction for voices with only a few sharp S sounds.'),
    'Natural De-Essing': ('Etwas bestimmter als Default, mit schmalerem Band: das S wird ruhiger, die Luft bleibt.',
                          'A little firmer than Default with a narrower band: the S calms down, the air stays.'),
    'Transparent': ('Schmales Band nur auf dem S-Kern: kaum Klangfärbung, Luft und Glanz bleiben fast ganz.',
                    'Narrow band on the core of the S only: hardly any colouring, air and sheen stay almost untouched.'),
    'Subtle Reduction': ('Die leichteste Einstellung: nimmt nur die Spitzen der S etwas zurück.',
                         'The lightest setting: only takes the tips of the S sounds back a little.'),
    'Strong Sibilance': ('Für deutlich zu scharfe S: kräftige Absenkung, schnellere Erholung.',
                         'For clearly too sharp S sounds: strong reduction, faster recovery.'),
    'Male Rap': ('Tiefer S-Bereich und schnelle Erholung für Rap mit tiefer bis mittlerer Männerstimme.',
                 'Low S range and fast recovery for rap with a low to mid male voice.'),
    'Deep Male Vocal': ('S-Bereich ab 4,8 kHz für tiefe Männerstimmen, deren S tief und dunkel liegen.',
                        'S range from 4.8 kHz for deep male voices whose S sounds sit low and dark.'),
    'Bright Male Vocal': ('Höherer, schmalerer S-Bereich für helle Männerstimmen und Tenöre.',
                          'Higher, narrower S range for bright male voices and tenors.'),
    'Aggressive Rap': ('Kräftig und schnell für harten, dichten Rap mit vielen scharfen S.',
                       'Strong and fast for hard, dense rap with many sharp S sounds.'),
    'Modern Pop Male': ('Für glänzend produzierte Pop-Männerstimmen: bestimmt, Luft bleibt.',
                        'For shiny, produced male pop vocals: firm, the air stays.'),
    'Female Pop': ('Ausgewogen für Pop-Frauenstimmen: S ruhiger, Glanz bleibt.',
                   'Balanced for female pop vocals: calmer S, the sheen stays.'),
    'Bright Female Vocal': ('Hoher, schmaler S-Bereich für sehr helle Frauenstimmen mit spitzen S.',
                            'High, narrow S range for very bright female voices with piercing S sounds.'),
    'Airy Vocal': ('Sehr schmales Band nur auf dem S: gehauchte, luftige Stimmen behalten ihre Luft.',
                   'Very narrow band on the S only: breathy, airy voices keep their air.'),
    'Soft Female Vocal': ('Sanft und langsam für leise, weiche Frauenstimmen und Balladen.',
                          'Gentle and slow for quiet, soft female voices and ballads.'),
    'Powerful Female Vocal': ('Kräftig für laute, gepresste Frauenstimmen mit harten S.',
                              'Strong for loud, belting female voices with hard S sounds.'),
    'Hypertechno': ('Für stark komprimierte, hell gemischte Vocals bei hohem Tempo: kräftig, schnelle Erholung.',
                    'For heavily compressed, brightly mixed vocals at high tempo: strong, fast recovery.'),
    'EDM Lead Vocal': ('Für komprimierte, helle EDM-Lead-Vocals: bestimmt und schnell.',
                       'For compressed, bright EDM lead vocals: firm and fast.'),
    'Dance Pop': ('Für helle, komprimierte Dance-Pop-Vocals: deutlich, aber natürlich.',
                  'For bright, compressed dance pop vocals: clear but natural.'),
    'Hard Dance': ('Die stärkste Genre-Einstellung: für extrem komprimierte, sehr helle Vocals, breites Band, sehr schnell.',
                   'The strongest genre setting: for extremely compressed, very bright vocals, wide band, very fast.'),
    'Melodic Techno': ('Weich und langsam für flächige, hallige Vocals: kein Pumpen.',
                       'Smooth and slow for wide, reverb-heavy vocals: no pumping.'),
    'Trap Vocals': ('Tiefer S-Bereich, kräftig und schnell für komprimierte Trap-Vocals mit viel Höhen.',
                    'Low S range, strong and fast for compressed trap vocals with a lot of top end.'),
    'Modern Hip-Hop': ('Tiefer S-Bereich, bestimmt für komprimierte Hip-Hop-Vocals.',
                       'Low S range, firm for compressed hip-hop vocals.'),
    'Acoustic Vocals': ('Behutsam und langsam für natürliche, wenig bearbeitete Aufnahmen.',
                        'Careful and slow for natural, lightly processed recordings.'),
    'Harsh S Sounds': ('Schmal und kräftig auf dem S-Kern: zähmt stechende S, ohne die Luft zu nehmen.',
                       'Narrow and strong on the core of the S: tames stinging S sounds without taking the air.'),
    'Extreme Sibilance': ('Maximale Absenkung für extrem scharfe Aufnahmen. Mit Delta prüfen, ob es lispelt.',
                          'Maximum reduction for extremely sharp recordings. Check with Delta that it does not lisp.'),
    'Sharp T Sounds': ('Sehr schneller Attack: fängt auch kurze t-, k- und z-Spitzen am Anfang.',
                       'Very fast attack: also catches short t, k and ts peaks right at their start.'),
    'Vocal Recording Rescue': ('Für problematische Aufnahmen (helles Mikrofon, zu nah): großer Range, früher Einsatz.',
                               'For problem recordings (bright microphone, too close): wide range, early start.'),
    'Mix / Bus': ('Allround-Erkennung für einen ganzen Mix, eine Gruppe oder Instrumente statt einer Solostimme.',
                  'Allround detection for a whole mix, a group or instruments instead of a single vocal.'),
}
CAT = {'basics': 'vocalBasics', 'male': 'maleVocals', 'female': 'femaleVocals', 'genre': 'genre', 'special': 'specialPurpose'}


def num(x):
    s = ('%.1f' % x).rstrip('0').rstrip('.') if x != int(x) else '%d.0' % x
    return s + 'f'


if __name__ == '__main__':
    import sys
    sys.stdout.reconfigure(encoding='utf-8')
    for p in P:
        name, cat, _, f, t, r, a, at, rel, la, w, d = p
        if cat == 'none':
            continue
        de, en = DESC[name]
        det = '' if d == 0 else ', %s' % num(d)
        print('            { C::%s, "%s",' % (CAT[cat], name))
        print('              "%s",' % de)
        print('              "%s",' % en)
        print('              sound (%s, %s, %s, %s, %s, %s, %s, %s%s) },' % (num(f), num(t), num(r), num(a), num(at), num(rel), num(la), num(w), det))
