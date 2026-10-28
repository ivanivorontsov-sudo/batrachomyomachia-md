#!/usr/bin/env python3
"""1-player emulator script: the human (mice) plays the host's easy moves, the console AI (hard) answers.
The hard level has no random noise and a node budget, so the ROM and the host pick the same moves."""
import sys, subprocess
seed = sys.argv[1] if len(sys.argv) > 1 else '3'
out = subprocess.run(['tests/gen_script', '0', seed, '0', '2'], capture_output=True, text=True).stdout.splitlines()
def rc(sq):
    r = sq // 4; return r, 2*(sq % 4) + (1 if r % 2 == 0 else 0)
# title -> menu; Сложность -> Трудно; skip the intro
L = ['wait 120', 'press S 2', 'wait 40', 'press D 2', 'press D 2', 'press A 2', 'press U 2', 'press U 2',
     'press S 2', 'wait 60', 'press S 2', 'wait 60']
cur = [5, 0]
def go(r, c):
    while cur[0] < r: L.append('press D 1'); cur[0] += 1
    while cur[0] > r: L.append('press U 1'); cur[0] -= 1
    while cur[1] < c: L.append('press R 1'); cur[1] += 1
    while cur[1] > c: L.append('press L 1'); cur[1] -= 1
ply = 0; thought = False
for line in out:
    if line.startswith('END'): break
    a, meta = line.split('|'); sq = list(map(int, a.split()))
    if ply % 2 == 0:
        L.append('wait 20'); go(*rc(sq[0])); L.append('press A 1')
        for t in sq[1:]: go(*rc(t)); L.append('press A 1')
        if not thought and ply >= 6: L += ['wait 100', 'shot ai_thinking.ppm', 'wait 500']; thought = True
        else: L.append('wait 600')
    ply += 1
L += ['wait 300', 'shot defeat.ppm']
open('shots/s1p.txt', 'w').write('\n'.join(L) + '\n')
print(len(L), 'commands', ply, 'plies')
