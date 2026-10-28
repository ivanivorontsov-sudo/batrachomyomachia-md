#!/usr/bin/env python3
"""Original music (YM2612+PSG VGM, compiled to XGM2 by rescomp) and PCM sound effects.
Instruments: 'kithara' (FM pluck), 'aulos' (FM reed pipe), frame drum (PSG noise). Mode: D Dorian."""
import struct, os, math, wave
import numpy as np
ROOT=os.path.dirname(os.path.abspath(__file__))+'/..'
FMCLK=7670453; PSGCLK=3579545; FRAME=735

NOTE={'C':0,'C#':1,'D':2,'D#':3,'E':4,'F':5,'F#':6,'G':7,'G#':8,'A':9,'A#':10,'B':11}
def midi(n):  # 'D5' 'F#4'
    name,octv=n[:-1],int(n[-1]); return 12*(octv+1)+NOTE[name]
def hz(m): return 440*2**((m-69)/12)

class VGM:
    def __init__(s): s.d=bytearray(); s.samples=0; s.loop_ofs=None; s.loop_samples=0
    def ym(s,port,reg,val): s.d+=bytes([0x52+port,reg,val&0xFF])
    def psg(s,v): s.d+=bytes([0x50,v&0xFF])
    def wait(s,n=1):
        for _ in range(n): s.d.append(0x62); s.samples+=FRAME
    def mark_loop(s): s.loop_ofs=len(s.d); s.loop_at=s.samples
    def save(s,path):
        s.d.append(0x66)
        h=bytearray(0x40)
        h[0:4]=b'Vgm '; struct.pack_into('<I',h,8,0x150)
        struct.pack_into('<I',h,0x0C,PSGCLK); struct.pack_into('<I',h,0x18,s.samples)
        if s.loop_ofs is not None:
            struct.pack_into('<I',h,0x1C,0x40+s.loop_ofs-0x1C); struct.pack_into('<I',h,0x20,s.samples-s.loop_at)
        struct.pack_into('<I',h,0x24,60); struct.pack_into('<H',h,0x28,0x0009); h[0x2A]=16
        struct.pack_into('<I',h,0x2C,FMCLK); struct.pack_into('<I',h,0x34,0x40-0x34)
        data=h+s.d; struct.pack_into('<I',data,4,len(data)-4)
        open(path,'wb').write(data)

# patches: per operator (S1,S3,S2,S4 register order) -> (DT_MUL, TL, RS_AR, D1R, D2R, SL_RR)
KITHARA=dict(fbalg=(4<<3)|4, ops=[(0x01,30,0x1F,0x0E,0x06,0x5A),(0x03,34,0x1F,0x10,0x06,0x5A),
                                  (0x01,0,0x1F,0x0C,0x05,0x48),(0x02,10,0x1F,0x0D,0x06,0x48)])
AULOS=dict(fbalg=(5<<3)|4, ops=[(0x02,34,0x12,0x04,0x02,0x26),(0x01,40,0x14,0x04,0x02,0x26),
                                (0x01,4,0x12,0x02,0x01,0x16),(0x02,14,0x12,0x02,0x01,0x16)])
BASS=dict(fbalg=(3<<3)|4, ops=[(0x00,26,0x1F,0x0C,0x05,0x4A),(0x01,30,0x1F,0x0E,0x05,0x4A),
                               (0x00,2,0x1F,0x0A,0x04,0x3A),(0x01,12,0x1F,0x0B,0x04,0x3A)])
def chreg(ch): return (0 if ch<3 else 1), ch%3
def setpatch(v,ch,p,vol=0):
    port,c=chreg(ch)
    for i,op in enumerate(p['ops']):
        o=[0,4,8,12][i]
        dm,tl,ar,d1,d2,slrr=op
        carrier = i>=2
        v.ym(port,0x30+o+c,dm); v.ym(port,0x40+o+c,min(127,tl+(vol if carrier else 0)))
        v.ym(port,0x50+o+c,ar); v.ym(port,0x60+o+c,d1); v.ym(port,0x70+o+c,d2); v.ym(port,0x80+o+c,slrr)
        v.ym(port,0x90+o+c,0)
    v.ym(port,0xB0+c,p['fbalg']); v.ym(port,0xB4+c,0xC0)
def keycode(ch): return [0,1,2,4,5,6][ch]
def fm_note(v,ch,m):
    port,c=chreg(ch); f=hz(m); blk=0
    fn=f*144*2**20/FMCLK
    while fn>=2048 and blk<7: fn/=2; blk+=1
    # move into upper range for precision
    while fn<1024 and blk>0: fn*=2; blk-=1
    fn=int(round(fn))
    fn=f*144*2**20/FMCLK/2**(blk-1) if blk>0 else f*144*2**20/FMCLK*2
    fn=int(round(fn))
    while fn>2047: blk+=1; fn=int(round(f*144*2**20/FMCLK/2**(blk-1)))
    v.ym(port,0xA4+c,(blk<<3)|(fn>>8)); v.ym(port,0xA0+c,fn&0xFF)
    v.ym(0,0x28,0xF0|keycode(ch))
def fm_off(v,ch): v.ym(0,0x28,keycode(ch))

def parse(seq):
    """'A4:4 D5:4 r:2' -> [(midi|None, 16ths)]"""
    out=[]
    for tok in seq.split():
        n,d=tok.split(':'); out.append((None if n=='r' else midi(n),int(d)))
    return out

CH={'Dm':['D3','A3','D4','F4'],'C':['C3','G3','C4','E4'],'G':['G2','D3','G3','B3'],'Am':['A2','E3','A3','C4'],
    'F':['F2','C3','F3','A3'],'Em':['E2','B2','E3','G3'],'A':['A2','E3','A3','C#4'],'D':['D3','A3','D4','F#4']}

def render(path, mel, chords, step, loop=True, drums='dum', arp=True, lead=AULOS, intro_bars=0, tail=60):
    """mel: list of (midi,len16); chords: list per bar; step: frames per 16th"""
    ev={}  # frame -> list of callables
    def at(f,fn): ev.setdefault(f,[]).append(fn)
    v=VGM()
    # channel map: 0 melody, 1 arp, 2 bass, 3 melody echo (kithara doubling)
    setpatch(v,0,lead,0); setpatch(v,1,KITHARA,10); setpatch(v,2,BASS,2); setpatch(v,3,KITHARA,14)
    v.ym(0,0x22,0); v.ym(0,0x27,0); v.ym(0,0x2B,0)
    for ch in range(4): fm_off(v,ch)
    v.psg(0x9F); v.psg(0xBF); v.psg(0xDF); v.psg(0xFF)
    bar=16*step; total=len(chords)*bar
    t=0
    for (m,d) in mel:
        if m is not None:
            at(t,lambda v,m=m:fm_note(v,0,m))
            if d>=4: at(t+2,lambda v,m=m:fm_note(v,3,m-12))
            if d*step>2: at(t+d*step-1,lambda v:fm_off(v,0))
        t+=d*step
    for bi,c in enumerate(chords):
        notes=[midi(x) for x in CH[c]]; b0=bi*bar
        at(b0,lambda v,m=notes[0]-12:fm_note(v,2,m)); at(b0+6*step,lambda v:fm_off(v,2))
        at(b0+8*step,lambda v,m=notes[0]:fm_note(v,2,m)); at(b0+bar-2,lambda v:fm_off(v,2))
        if arp:
            pat=[1,2,3,2,1,2,3,2,1,2,3,2,1,2,3,2] if c not in ('A','D') else [1,2,3,1,2,3,1,2,3,1,2,3,1,2,3,2]
            for i in range(0,16,2):
                at(b0+i*step,lambda v,m=notes[pat[i]]+12:fm_note(v,1,m)); at(b0+(i+2)*step-1,lambda v:fm_off(v,1))
        if drums:
            hits={'dum':[(0,0),(6,2),(8,1),(12,2),(14,2)],'slow':[(0,0),(8,1)],'none':[]}[drums]
            for pos,kind in hits:
                att=[0,3,7][kind]; per=[0xE6,0xE5,0xE4][kind]   # noise: white, shift rates
                def hit(v,att=att,per=per,step=step):
                    v.psg(per); v.psg(0xF0|att)
                at(b0+pos*step,hit)
                for k in range(1,6): at(b0+pos*step+k,lambda v,a=min(15,att+k*3):v.psg(0xF0|a))
    for f in range(total+(0 if loop else tail)):
        if loop and f==0: v.mark_loop()
        for fn in ev.get(f,[]): fn(v)
        v.wait()
    if not loop:
        for ch in range(4): fm_off(v,ch)
        v.psg(0xFF)
    v.save(path)

TITLE_MEL=parse('''A4:4 D5:4 F5:2 E5:2 D5:4  E5:4 G5:4 E5:4 C5:4  D5:6 B4:2 G4:4 B4:4  A4:8 C5:4 E5:4
F5:4 E5:2 D5:2 A5:8  G5:4 E5:2 C5:2 E5:8  D5:4 B4:4 G4:4 B4:4  D5:16''')
TITLE_CH=['Dm','C','G','Am','Dm','C','G','Dm']

GAME_MEL=parse('''D5:6 E5:2 F5:4 E5:4  C5:8 r:4 G4:4  A4:6 B4:2 C5:4 D5:4  E5:12 r:4
F5:4 G5:4 A5:4 G5:2 F5:2  E5:8 C5:8  D5:6 C5:2 B4:4 G4:4  A4:12 r:4
A4:2 C5:2 D5:4 D5:2 E5:2 F5:4  G5:6 F5:2 E5:4 C5:4  D5:4 A4:4 B4:4 G4:4  A4:16
D5:4 F5:4 A5:4 G5:4  F5:4 E5:4 C5:8  B4:4 C5:2 B4:2 G4:8  D5:16''')
GAME_CH=['Dm','C','F','Am','Dm','C','G','Am','F','C','G','Am','Dm','Am','G','Dm']

WIN_MEL=parse('D5:2 F#5:2 A5:4 F#5:2 A5:2 D6:8  B5:2 A5:2 G5:2 F#5:2 E5:4 A5:4 D6:16 r:16')
WIN_CH=['D','G','D','D']
LOSE_MEL=parse('A5:8 G5:4 F5:4  E5:8 D5:8  C5:6 B4:2 A4:8  D4:16 r:16')
LOSE_CH=['Dm','Am','G','Dm','Dm']

def ks(freq,dur,sr=13300,decay=0.996,bright=0.5,seed=1):
    rng=np.random.RandomState(seed); n=int(sr/freq); buf=rng.uniform(-1,1,n)
    out=np.zeros(int(sr*dur))
    for i in range(len(out)):
        out[i]=buf[i%n]; nxt=buf[(i+1)%n]
        buf[i%n]=decay*(bright*buf[i%n]+(1-bright)*nxt) if True else 0
    return out
def save_wav(path,x,sr=13300):
    x=x/max(1e-6,np.abs(x).max())*0.9
    d=(x*32767).astype('<i2')
    w=wave.open(path,'wb'); w.setnchannels(1); w.setsampwidth(2); w.setframerate(sr); w.writeframes(d.tobytes()); w.close()

def sfx():
    sr=13300; o=ROOT+'/res/sfx/'
    # move: wooden 'tok' on the board
    t=np.arange(int(sr*0.09))/sr
    x=np.sin(2*np.pi*520*t*(1-0.4*t/0.09))*np.exp(-t*60)+0.3*np.random.RandomState(2).uniform(-1,1,len(t))*np.exp(-t*200)
    save_wav(o+'move.wav',x)
    # capture: bronze clang (inharmonic partials) + noise
    t=np.arange(int(sr*0.45))/sr
    x=sum(a*np.sin(2*np.pi*f*t)*np.exp(-t*k) for f,a,k in ((310,1,9),(742,.7,12),(1213,.5,16),(1871,.35,22),(2630,.2,30)))
    x+=0.8*np.random.RandomState(3).uniform(-1,1,len(t))*np.exp(-t*40)
    save_wav(o+'capture.wav',x)
    # splash (frog captured): filtered noise burst
    n=np.random.RandomState(4).uniform(-1,1,int(sr*0.4)); t=np.arange(len(n))/sr
    y=np.convolve(n,np.ones(4)/4,'same')*np.exp(-t*9)*(1-np.exp(-t*200))
    save_wav(o+'splash.wav',y)
    # promotion: lyre glissando up D minor pentatonic
    seq=[62,65,67,69,72,74,77,81]; parts=[]
    for i,m in enumerate(seq):
        parts.append(ks(hz(m),0.09 if i<7 else 0.6,decay=0.997,seed=i))
    tot=np.zeros(int(sr*(0.09*7+0.6))+10)
    for i,p in enumerate(parts):
        s=int(i*0.09*sr); tot[s:s+len(p)]+=p
    save_wav(o+'promote.wav',tot)
    # select: single high pluck
    save_wav(o+'select.wav',ks(hz(81),0.18,decay=0.993,seed=9))
    # error: low dull double pluck
    a=ks(hz(45),0.14,decay=0.98,bright=0.8,seed=5); b=ks(hz(44),0.2,decay=0.98,bright=0.8,seed=6)
    save_wav(o+'error.wav',np.concatenate([a,b]))

if __name__=='__main__':
    m=ROOT+'/res/music/'
    render(m+'title.vgm',TITLE_MEL,TITLE_CH,8,drums='dum')
    render(m+'game.vgm',GAME_MEL,GAME_CH,10,drums='slow',lead=KITHARA)
    render(m+'victory.vgm',WIN_MEL,WIN_CH,7,loop=False,drums='dum')
    render(m+'defeat.vgm',LOSE_MEL,LOSE_CH,10,loop=False,drums='slow')
    sfx(); print('music ok')
