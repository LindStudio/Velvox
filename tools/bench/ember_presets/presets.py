"""Factory preset candidates (r2/ember-presets). name -> (category, material, values)
values: freq thr range amount(%) attack release la width det
material: pb.py take spec ('mix' = synthetic mix via mix.py)
"""
P = [
    # name, category, material, freq, thr, range, amount, attack, release, la, width, det
    ('Default', 'none', 'all', 6000, -34, 5, 50, 0.5, 60, 5, 3, 0),
    ('Gentle Vocal', 'basics', 'all', 6000, -31, 3.5, 50, 0.5, 70, 5, 2.5, 0),
    ('Natural De-Essing', 'basics', 'all', 6000, -35.5, 5.5, 50, 0.5, 50, 5, 2.5, 0),
    ('Transparent', 'basics', 'all', 6000, -34.5, 4.5, 50, 0.3, 60, 6, 1.8, 0),
    ('Subtle Reduction', 'basics', 'all', 6000, -30, 2.5, 50, 0.5, 70, 5, 3, 0),
    ('Strong Sibilance', 'basics', 'all', 6000, -38, 7.5, 50, 0.5, 45, 5, 3, 0),
    ('Male Rap', 'male', 'M36,M40,M42,M_emerald,M_allittakes', 5300, -36.5, 6, 50, 0.3, 35, 5, 3, 0),
    ('Deep Male Vocal', 'male', 'M36,M40,M42,M_emerald', 4800, -36, 5, 50, 0.5, 60, 5, 3, 0),
    ('Bright Male Vocal', 'male', 'M_notme,M_allittakes', 6600, -34.5, 5, 50, 0.5, 55, 5, 2.5, 0),
    ('Aggressive Rap', 'male', 'M36,M40,M42,M_emerald,M_allittakes', 5400, -39, 8, 60, 0.3, 30, 5, 3, 0),
    ('Modern Pop Male', 'male', 'male', 6300, -35.5, 5.5, 50, 0.5, 45, 5, 2.5, 0),
    ('Female Pop', 'female', 'female', 6300, -34.5, 5, 50, 0.5, 45, 5, 2.5, 0),
    ('Bright Female Vocal', 'female', 'F01,F_badboy,F_runwild', 6200, -36, 6, 50, 0.5, 45, 5, 2.5, 0),
    ('Airy Vocal', 'female', 'F01,F08,F22,F_badboy,F_runwild', 6000, -34.5, 5, 50, 0.5, 50, 5, 1.5, 0),
    ('Soft Female Vocal', 'female', 'F01,F08,F22,F_wings', 6200, -31, 3.5, 50, 0.7, 80, 5, 2.5, 0),
    ('Powerful Female Vocal', 'female', 'female', 6000, -37.5, 7, 50, 0.4, 40, 5, 3, 0),
    ('Hypertechno', 'genre', 'G_F01_club,G_F22_club,G_F_runwild_club,G_J1_chemicals_club,G_M_notme_club', 6400, -37, 7, 50, 0.3, 40, 5, 3, 0),
    ('EDM Lead Vocal', 'genre', 'G_F01_club,G_F22_club,G_F_runwild_club,G_J1_chemicals_club,G_M_notme_club', 6300, -36, 6, 50, 0.4, 45, 5, 3, 0),
    ('Dance Pop', 'genre', 'G_F01_club,G_F22_club,G_F_runwild_club,G_J1_chemicals_club,G_M_notme_club', 6200, -35.5, 5.5, 50, 0.4, 35, 5, 2.5, 0),
    ('Hard Dance', 'genre', 'G_F01_club,G_F22_club,G_F_runwild_club,G_J1_chemicals_club,G_M_notme_club', 6200, -38.5, 8, 55, 0.2, 30, 5, 3, 0),
    ('Melodic Techno', 'genre', 'G_F01_club,G_F22_club,G_F_runwild_club,G_J1_chemicals_club,G_M_notme_club', 6100, -34.5, 5, 50, 0.6, 80, 5, 2.5, 0),
    ('Trap Vocals', 'genre', 'G_M36_trap,G_M40_trap,G_M_emerald_trap,G_M_allittakes_trap', 5400, -37, 6.5, 50, 0.3, 30, 5, 3, 0),
    ('Modern Hip-Hop', 'genre', 'G_M36_trap,G_M40_trap,G_M_emerald_trap,G_M_allittakes_trap', 5500, -35.5, 5.5, 50, 0.4, 45, 5, 3, 0),
    ('Acoustic Vocals', 'genre', 'F08,F22,J1_chemicals,M_allittakes,F_wings', 6300, -31.5, 3.5, 45, 0.6, 90, 5, 2.5, 0),
    ('Harsh S Sounds', 'special', 'all', 6000, -38, 8, 50, 0.3, 45, 5, 2.5, 0),
    ('Extreme Sibilance', 'special', 'all', 6000, -42, 10, 65, 0.3, 45, 5, 3, 0),
    ('Sharp T Sounds', 'special', 'all', 6400, -38, 6, 50, 0.1, 50, 5, 3, 0),
    ('Vocal Recording Rescue', 'special', 'all', 6000, -39.5, 10, 50, 0.2, 55, 8, 3, 0),
    ('Mix / Bus', 'special', 'mix', 6000, -41, 4.5, 50, 0.5, 45, 5, 3, 3),
]


def args(p):
    _, _, _, f, t, r, a, at, rel, la, w, d = p
    return 'freq=%g,thr=%g,range=%g,amount=%g,attack=%g,release=%g,la=%g,width=%g,det=%d' % (f, t, r, a / 100, at, rel, la, w, d)


def key(p):
    return p[0].replace(' ', '_').replace('/', '').replace('-', '')
