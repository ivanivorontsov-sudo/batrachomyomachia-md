#!/usr/bin/env python3
"""Turns a move list from gen_script into an emulator input script for 2-player (hotseat) mode."""
import sys, subprocess
rules, seed = sys.argv[1], sys.argv[2]
out = subprocess.run(['tests/gen_script', rules, seed, '2', '0'], capture_output=True, text=True).stdout.splitlines()
def rc(sq):
    r = sq // 4; return r, 2*(sq % 4) + (1 if r % 2 == 0 else 0)
L = ['wait 120', 'press S 2', 'wait 40', 'press A 2', 'wait 10']        # title -> menu, players: 2
if rules == '1': L += ['press D 2', 'press D 2', 'press D 2', 'press A 2', 'press U 2', 'press U 2', 'press U 2']
L += ['press S 2', 'wait 60', 'press S 2', 'wait 60', 'shot game_start.ppm']  # skip intro
cur = [5, 0]
def go(r, c):
    while cur[0] < r: L.append('press D 1'); cur[0] += 1
    while cur[0] > r: L.append('press U 1'); cur[0] -= 1
    while cur[1] < c: L.append('press R 1'); cur[1] += 1
    while cur[1] > c: L.append('press L 1'); cur[1] -= 1
shot_capt = shot_promo = shot_multi = False; mv = 0
for line in out:
    if line.startswith('END'): break
    a, meta = line.split('|'); sq = list(map(int, a.split()))
    capt = int(meta.split('capt=')[1].split()[0]); promo = 'promo=1' in meta
    go(*rc(sq[0])); L.append('press A 1')
    for i, t in enumerate(sq[1:]):
        go(*rc(t)); 
        if i == len(sq) - 2 and capt and not shot_capt:
            L.append('shot select_capture.ppm')
        L.append('press A 1')
    # grab frames during the animation of interesting moves
    if capt >= 2 and not shot_multi: L += ['wait 30', 'shot multi_capture_anim.ppm', 'wait 120', 'shot multi_capture_done.ppm']; shot_multi = True
    elif capt and not shot_capt: L += ['wait 14', 'shot capture_anim.ppm', 'wait 60', 'shot capture_done.ppm']; shot_capt = True
    elif promo and not shot_promo: L += ['wait 90', 'shot king.ppm']; shot_promo = True
    else: L.append('wait %d' % (40 + 50 * capt))
    mv += 1
    if mv == 20: L.append('shot midgame.ppm')
L += ['wait 120', 'shot end.ppm', 'wait 200', 'shot end2.ppm']
open('shots/s2.txt', 'w').write('\n'.join(L) + '\n')
print(len(L), 'commands', mv, 'moves')
