# Local script: it reads the notes in the layout of the maintainer's working folder (docs/archive/jstars-notes/Balance/NNN_name.md). In this repository the
# same notes are docs/character-details/ and docs/balance/, so point B at those (and change the file pattern) before running it here.
# Writes docs/archive/jstars-notes/Balance/Knockdowns.md: every fighter's hits whose reaction number knocks down (attack slot +0x39). It then also adds the same rows
# to each fighter's page as a "## Knockdowns" section (replaced on a rerun).
import struct, glob, os, re, collections
# KD_DUMPS: the folder holding the fighters' unpacked battle data (cast\battle_NNN\*param_attack*, cast_bullet\battle_NNN.bin).
S = os.environ.get('KD_DUMPS', 'dumps')
B = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'Balance')
AT, HIT = 0x618, 0x9C
# reaction number -> what the dummy did (run kv1, Gintoki on a 0-star Koro-sensei); True = ends on the ground
R = {0: ('flinch', False), 1: ('flinch (dmg_front)', False), 2: ('heavy flinch (dmg_heavy_front)', False),
     3: ('lifted off the feet; lands and goes down unless hit again', True), 4: ('knocked down, big bounce', True), 5: ('spun away (dmg_blow_spin)', True),
     6: ('knocked away (dmg_blow)', True), 8: ('knocked down, short bounce', True), 9: ('knocked down, big bounce', True),
     10: ('knocked down, rolling bounce', True), 11: ('shocked, then down (dmg_shock)', True), 12: ('pierced, then down (dmg_penetration)', True),
     14: ('falls (dmg_seiya_fall), then down', True), 15: ('held (dmg_dio_blood_suck), no knockdown', False),
     17: ('big knock away (dmg_conspicuous_blow)', True), 18: ('big bounce (dmg_conspicuous_bound)', True),
     19: ('knocked away (dmg_kizuna1_hit_blow)', True), 20: ('big bounce (dmg_conspicuous_bound 2)', True),
     21: ('launched, then down', True), 22: ('flinch', False), 23: ('middle flinch (dmg_middle_front)', False),
     24: ('falls flat (dmg_prostrate), gets up on the spot', False), 26: ('no reaction seen', False)}
names = {}
moves = {}
for f in glob.glob(B + r'\[0-9][0-9][0-9]_*.md'):
    c = int(os.path.basename(f)[:3]); names[c] = os.path.basename(f)[4:-3].replace('_new', '').replace('_', ' ').title()
    for line in open(f, encoding='utf-8'):
        if not line.startswith('|'): continue
        cells = [x.strip() for x in line.strip().strip('|').split('|')]
        for x in cells[1:]:
            m = re.fullmatch(r'Rows? (\d+)', x)
            if m and cells[0] and not cells[0].startswith('-'): moves.setdefault((c, int(m.group(1))), cells[0])
out = ['# Knockdowns by character', '',
       'Which hits knock the target down, for Balance. Read from each fighter\'s `param_attack` data (2026-11-03). How it was found: Shared.md, "Knockdown and getting up".', '',
       '## The reaction number', '',
       'Each hit (attack slot, 0x9C bytes; row r hit h at file + 0x10 + r x 0x618 + h x 0x9C) has its reaction at **+0x39**. Changing it changes what the hit does (tested: run kv1, every value below put on Gintoki\'s Square and used on a 0-star dummy). After every knockdown the target lies on the ground about 1.1 s, then gets up (`dmg_recover`, about 0.95 s).', '',
       '| Number | What the target did | Knocks down |', '|---|---|---|']
for v in sorted(R): out.append('| %d | %s | %s |' % (v, R[v][0], 'yes' if R[v][1] else 'no'))
out += ['', 'Numbers seen in the data but not tried: 128, 204, 255 (a few slots, likely unused).', '',
        '**Projectiles** (`eff_bullet`, 232-byte rows from +0x10) use the same numbers at **+0xAF** (tested, kd4: set to 1, Gintoki\'s special combo ender stopped knocking down).', '',
        '**Too many hits:** every hit also adds its build-up number (the first 16 bits of attack slot +0x30, the number the Balance pages called "gauge") to the target\'s build-up (health object +0x1A4). It drains by itself, about 1 to 2 per second. When it reaches **150** the target is spun away and knocked down whatever the hit\'s reaction, and it goes back to 0 (tested, st1: 144 plus a hit of 5 stayed standing, 146 plus 5 went down).', '',
        '**Getting up:** the target lies about 1.1 s, gets up in about 0.93 s, and cannot be hit until about 2.0 s after the get-up starts (pin3, pin4, with the dummy held in place). The get-up action file (`dmg_recover`) holds **64** at +0x40 for every fighter; set to 4, the target stood up at once and was hit again 0.8 to 0.9 s later (pin5). Which field marks the immunity itself is not found: holding two candidates at 0 changed nothing (pin4).', '',
        '## By character', '', 'Only hits with damage and a knockdown number. "Hit" is the attack slot, counting slots that deal no damage, so it can be higher than the position in a fighter\'s damage list. Each hit\'s build-up (its "too many hits" number) is in the Knockdowns section of that fighter\'s page. "Move" is the input from that fighter\'s page where the row is known; rows without one are not tied to an input yet.', '']
def proj(c):
    fb = S + r'\cast_bullet\battle_%03d.bin' % c
    if not os.path.exists(fb): return []
    b = open(fb, 'rb').read(); m = (len(b) - 16) // 232; res = []
    for r in range(m):
        q = b[16 + r * 232:16 + (r + 1) * 232]
        d = struct.unpack('>I', q[0x9C:0xA0])[0]
        if d and q[0xAF] in R and R[q[0xAF]][1]: res.append((r, q[0xAF], d))
    return res
tot = collections.Counter()
for c in range(42):
    fs = glob.glob(S + r'\cast\battle_%03d\*param_attack*' % c)
    if not fs: continue
    a = open(fs[0], 'rb').read(); n = struct.unpack('>I', a[:4])[0]
    rows = collections.OrderedDict()
    for k in range(n):
        o = 0x10 + k * HIT
        if o + HIT > len(a): break
        dmg = struct.unpack('>I', a[o + 0x28:o + 0x2C])[0]; v = a[o + 0x39]
        if not dmg or v not in R or not R[v][1]: continue
        rows.setdefault(k // 10, []).append((k % 10, v, dmg))
    nm = names.get(c, 'Fighter %d' % c)
    out.append('### %03d %s' % (c, nm)); out.append('')
    if not rows: out += ['No knockdown hits in the attack data.', '']; continue
    out += ['| Row | Move | Knockdown hits: hit (number, damage) |', '|---|---|---|']
    for r, hs in rows.items():
        out.append('| %d | %s | %s |' % (r, moves.get((c, r), ''), ', '.join('%d (%d, %d)' % h for h in hs)))
        tot[c] += len(hs)
    out.append('')
    pr = proj(c)
    if pr:
        out += ['| Projectile row | Number | Damage |', '|---|---|---|'] + ['| %d | %d | %d |' % x for x in pr] + ['']
open(B + r'\Knockdowns.md', 'w', encoding='utf-8', newline='\n').write('\n'.join(out).rstrip('\n') + '\n')
print(sum(tot.values()), 'knockdown hits in', len(tot), 'fighters')

# Each fighter's own page gets the same rows as a "## Knockdowns" section (replaced on a rerun), before "## Skill table".
for f in glob.glob(B + r'\[0-9][0-9][0-9]_*.md'):
    c = int(os.path.basename(f)[:3])
    fs = glob.glob(S + r'\cast\battle_%03d\*param_attack*' % c)
    if not fs: continue
    a = open(fs[0], 'rb').read(); n = struct.unpack('>I', a[:4])[0]
    rows = collections.OrderedDict()
    for k in range(n):
        o = 0x10 + k * HIT
        if o + HIT > len(a): break
        dmg = struct.unpack('>I', a[o + 0x28:o + 0x2C])[0]; v = a[o + 0x39]
        if dmg and v in R and R[v][1]: rows.setdefault(k // 10, []).append((k % 10, v, dmg, struct.unpack('>H', a[o + 0x30:o + 0x32])[0]))
    t = ['', '## Knockdowns', '',
         'Hits that knock the target down, from the attack data (2026-11-03). The number is the hit\'s reaction (attack slot +0x39, projectile row +0xAF); build-up is its "too many hits" number (the target goes down at 150). What each number does and the get-up are in [Knockdowns.md](Knockdowns.md).', '']
    if not rows: t.append('No knockdown hits in the attack data.')
    else:
        t += ['| Row | Move | Hit | Number | What it does | Damage | Build-up |', '|---|---|---|---|---|---|---|']
        for r, hs in rows.items():
            for h, v, d, bu in hs: t.append('| %d | %s | %d | %d | %s | %d | %d |' % (r, moves.get((c, r), ''), h, v, R[v][0], d, bu))
    pr = proj(c)
    if pr: t += ['', '| Projectile row | Number | What it does | Damage |', '|---|---|---|---|'] + ['| %d | %d | %s | %d |' % (r, v, R[v][0], d) for r, v, d in pr]
    s = open(f, encoding='utf-8').read()
    s = re.sub(r'\n## Knockdowns\n.*?(?=\n## |\Z)', '', s, flags=re.S)
    add = '\n'.join(t).rstrip('\n') + '\n'
    i = s.find('\n## Skill table')
    s = s[:i] + add + s[i:] if i >= 0 else s.rstrip('\n') + '\n' + add
    open(f, 'w', encoding='utf-8', newline='\n').write(s)
print('pages done')
