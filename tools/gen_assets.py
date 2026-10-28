#!/usr/bin/env python3
"""Generates all graphics for the ROM: tiles/palettes as C arrays + title IMAGE PNG for rescomp.
Every asset is produced here procedurally (silhouettes come from images generated for this project)."""
import math, os
from PIL import Image, ImageDraw, ImageFont, ImageFilter
import numpy as np
ROOT=os.path.dirname(os.path.abspath(__file__))+'/..'
ART=ROOT+'/tools/src_art'
FONT_PIX='/usr/share/fonts/truetype/sand-box/google/Press Start 2P/PressStart2P-Regular.ttf'
FONT_SERIF='/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf'

def md(r,g,b): return (r*36,g*36,b*36)   # 3-bit MD levels -> rgb
def vdp(c): r,g,b=c; return ((b//36)<<9)|((g//36)<<5)|((r//36)<<1)

PAL0=[md(0,0,0),md(0,0,0),md(1,0,0),md(4,1,0),md(6,3,1),md(7,5,2),md(7,6,4),md(7,6,1),
      md(5,3,0),md(6,6,5),md(4,4,3),md(3,1,0),md(2,1,0),md(1,2,4),md(2,4,6),md(7,7,7)]
PAL1=[md(0,0,0),md(0,0,0),md(2,1,0),md(6,3,1),md(7,5,2),md(7,7,5),md(4,2,0),md(6,4,1),
      md(7,7,2),md(0,2,0),md(2,5,1),md(5,7,2),md(2,2,2),md(5,5,5),md(3,5,7),md(6,1,0)]
PAL2=[md(0,0,0),md(0,0,0)]+[md(7,6,4)]*13+[md(7,7,6)]     # cream text, black shadow
PAL3=[md(0,0,0),md(1,0,0),md(7,7,0),md(4,7,1),md(7,2,0),md(7,5,0)]+[md(7,6,1)]*9+[md(7,6,1)]  # gold text + cursor colors

def tiles_from(img, w, h, colmajor=False):
    """img: 2D numpy array of palette indices. returns list of tiles (each 8 u32)"""
    out=[]
    tx,ty=w//8,h//8
    order=[(x,y) for x in range(tx) for y in range(ty)] if colmajor else [(x,y) for y in range(ty) for x in range(tx)]
    for (x,y) in order:
        t=[]
        for r in range(8):
            v=0
            for c in range(8): v=(v<<4)|int(img[y*8+r][x*8+c])
            t.append(v)
        out.append(t)
    return out

def silhouette(path, size, pad=0):
    im=Image.open(path).convert('L')
    a=np.array(im)<128
    ys,xs=np.where(a); im=im.crop((xs.min(),ys.min(),xs.max()+1,ys.max()+1))
    w,h=im.size; s=size/max(w,h)
    im=im.resize((max(1,round(w*s)),max(1,round(h*s))),Image.LANCZOS)
    return np.array(im)<140

MOUSE_PX=["##......##",
"###....###",
"###.##.###",
".########.",
".#o####o#.",
"..######..",
"...####...",
"....##....",
"..######..",
".########.",
".########.",
"..##..##..",]
FROG_PX=["...##....##...",
"..#o##..##o#..",
"..##########..",
"...########...",
".#.########.#.",
"##.########.##",
"#..########..#",
"...########...",
"..##########..",
".###......###.",
"##..........##",]
CROWN_PX=["k#k#k#k",
"k#k#k#k",
"k#####k",
"k#####k",
"kkkkkkk"]
def piece(size, side, king):
    S=size; g=np.zeros((S,S),dtype=np.uint8)
    c=(S-2)/2.0; R=(S-2)/2.0
    for y in range(S):
        for x in range(S):
            d=math.hypot(x+0.5-c,y+0.5-c); ds=math.hypot(x+0.5-c-1.3,y+0.5-c-1.6)
            if d<=R:
                lit=((x+0.5-c)+(y+0.5-c))/R   # -1.4 top-left .. +1.4 bottom-right
                if d>R-max(2,S/12):              # rim
                    if king: g[y,x]= 8 if lit<-0.3 else (7 if lit<0.6 else 6)
                    else:    g[y,x]= 7 if lit<-0.5 else (6 if lit<0.7 else 2)
                elif d>R-max(2,S/12)-1: g[y,x]=1
                else:
                    if side==0: g[y,x]= 4 if lit<-0.75 else 3
                    else:       g[y,x]= 2 if lit<-0.9 else 1
            elif ds<=R: g[y,x]=1
    if king:  # dotted rim: little gold/black studs like a hoplite aspis
        n=12 if S<40 else 20; rr=R-max(2,S/12)/2-0.3
        for i in range(n):
            a=2*math.pi*i/n; x=int(c+rr*math.cos(a)); y=int(c+rr*math.sin(a))
            if 0<=x<S and 0<=y<S: g[y,x]=5 if i%2==0 else 1
    # emblem
    inner=2*(R-max(2,S/12)-1)
    if S<40:
        pat = MOUSE_PX if side==0 else FROG_PX
        eh,ew=len(pat),len(pat[0])
        ox=int(round(c-ew/2)); oy=int(round(c-eh/2))+(2 if king else 0)
        for y,row in enumerate(pat):
            for x,ch in enumerate(row):
                gy,gx=oy+y,ox+x
                if ch=='#':
                    if side==0: g[gy,gx]=1
                    else:
                        edge=(y==0 or pat[y-1][x]!='#') or (x==0 or row[x-1]!='#')
                        g[gy,gx]=11 if edge else 10
                elif ch=='o': g[gy,gx]= 5 if side==0 else 1
        if king:
            cy0=oy-5
            for y,row in enumerate(CROWN_PX):
                for x,ch in enumerate(row):
                    gx=int(round(c-len(row)/2))+x
                    if ch=='#': g[cy0+y,gx]=8 if y<3 else 7
                    elif ch=='k': g[cy0+y,gx]=1
        return g
    esz=int(inner*0.62)
    sil=silhouette(ART+('/mouse.jpg' if side==0 else '/frog.jpg'), esz)
    eh,ew=sil.shape
    ox=int(round(c-ew/2)); oy=int(round(c-eh/2+inner*0.12))
    for y in range(eh):
        for x in range(ew):
            if sil[y,x]:
                if side==0: g[oy+y,ox+x]=1
                else:
                    edge = (y==0 or not sil[y-1,x]) or (x==0 or not sil[y,x-1])
                    g[oy+y,ox+x]= 11 if edge else 10
    if side==0:
        ey=oy+int(eh*0.30)
        for ex in (ox+int(ew*0.42),ox+int(ew*0.58)): g[ey,ex]=5; g[ey+1,ex]=5
    else:
        ey=oy+int(eh*0.12)
        for ex in (ox+int(ew*0.32),ox+int(ew*0.67)):
            g[ey,ex]=1; g[ey+1,ex]=1; g[ey,ex+1]=1; g[ey+1,ex+1]=8
    # crown scaled x2
    cy0=oy-12
    for y,row in enumerate(CROWN_PX):
        for x,ch in enumerate(row):
            for dy in (0,1):
                for dx in (0,1):
                    gx=int(round(c-len(row)))+2*x+dx; gy=cy0+2*y+dy
                    if ch=='#': g[gy,gx]=8 if y<3 else 7
                    elif ch=='k': g[gy,gx]=1
    return g

def marker(S=24):
    g=np.zeros((S,S),dtype=np.uint8); c=(S-1)/2
    for i in range(16):
        a=2*math.pi*i/16; x=int(round(c+6.5*math.cos(a))); y=int(round(c+6.5*math.sin(a)))
        g[y,x]=8 if i%2==0 else 0
    for dy in (-1,0,1):
        for dx in (-1,0,1):
            if abs(dx)+abs(dy)<2: g[int(c)+dy+1,int(c)+dx+1]=8 if dx==dy==0 else 7
    return g

def burst(S,phase):
    g=np.zeros((S,S),dtype=np.uint8); c=(S-1)/2; rng=np.random.RandomState(7+phase)
    r0=[4,8,11][phase]
    for i in range(14):
        a=2*math.pi*i/14+phase*0.3; rr=r0+rng.rand()*2
        x=int(round(c+rr*math.cos(a))); y=int(round(c+rr*math.sin(a)))
        if 0<=x<S and 0<=y<S:
            g[y,x]=[5,14,8][i%3]
            if phase<2 and x+1<S: g[y,x+1]=14
    if phase==0:
        for y in range(S):
            for x in range(S):
                if math.hypot(x-c,y-c)<3: g[y,x]=5
    return g

def brackets(S,col,ln=6,th=2):
    g=np.zeros((S,S),dtype=np.uint8)
    for t in range(th):
        for i in range(ln):
            for (x,y) in ((i,t),(t,i),(S-1-i,t),(S-1-t,i),(i,S-1-t),(t,S-1-i),(S-1-i,S-1-t),(S-1-t,S-1-i)):
                g[y,x]=col
    # dark outline for readability
    o=g.copy()
    for y in range(S):
        for x in range(S):
            if o[y,x]==0 and any(0<=y+dy<S and 0<=x+dx<S and o[y+dy,x+dx]==col for dy,dx in ((0,1),(1,0),(0,-1),(-1,0))): g[y,x]=1
    return g

# ---------------- board / frame (PAL0) ----------------
def light_sq():
    g=np.full((24,24),4,dtype=np.uint8)
    for y in range(24):
        for x in range(24):
            if (x*7+y*13)%23==0: g[y,x]=5
            if (x*5+y*11)%29==0: g[y,x]=3
    # small black-figure rosette (4 dots) in the centre
    for dx,dy in ((0,-2),(0,2),(-2,0),(2,0)): g[11+dy,11+dx]=3
    g[11,11]=3
    return g
def dark_sq(hl=False):
    g=np.full((24,24),11,dtype=np.uint8)
    for y in range(24):
        for x in range(24):
            if x==0 or y==0: g[y,x]=12
            elif x==23 or y==23: g[y,x]=3
            elif (x*3+y*7)%17==0: g[y,x]=12
    if hl:
        for i in range(24):
            for t in (1,2):
                for (x,y) in ((i,t),(t,i),(i,23-t),(23-t,i)):
                    if i<6 or i>17: g[y,x]=7
    return g
def meander_h():
    """8px tall band, 8px period Greek key, terracotta on black"""
    pat=["44444444",
         "........",
         "4444444.",
         "4.....4.",
         "4.444.4.",
         "4.4...4.",
         "4.44444.",
         "4......."]
    # a proper running key: period 8 with hook
    pat=["44444444",
         "........",
         ".44444..",
         ".4...4..",
         ".4.4.4..",
         ".4.4444.",
         ".4......",
         "44444444"]
    return np.array([[4 if ch=='4' else 1 for ch in row] for row in pat],dtype=np.uint8)
def meander_v(): return meander_h().T.copy()
def corner():
    pat=["44444444","4......4","4.4444.4","4.4..4.4","4.4..4.4","4.4444.4","4......4","44444444"]
    return np.array([[7 if ch=='4' else 1 for ch in row] for row in pat],dtype=np.uint8)
def column(h=26*8):
    """16px wide Ionic column, full height, marble"""
    g=np.full((h,16),1,dtype=np.uint8)
    for y in range(h):
        for x in range(16):
            if 2<=x<=13:
                fl=(x-2)%3
                g[y,x]= 9 if fl==1 else (10 if fl==2 else 15 if x<6 else 9)
            if x in (2,13): g[y,x]=10
    # capital (volutes) top 14px, base bottom 10px
    for y in range(14):
        for x in range(16): g[y,x]=1
    cap=["................",
         "9999999999999999",
         "9aaaaaaaaaaaaaa9",
         "................",
         ".99..999999..99.",
         "9aa9.9aaaa9.9aa9",
         "9a9a99....99a9a9",
         "9aa9.99999.9a9a9",
         ".99...9aa9...99.",
         "......9aa9......",
         "....99999999....",
         "....9aaaaaa9....",
         "................",
         "................"]
    for y,row in enumerate(cap):
        for x,ch in enumerate(row):
            if ch=='9': g[y,x]=9
            elif ch=='a': g[y,x]=10
    for y in range(h-10,h):
        for x in range(16): g[y,x]=1
    base=["....99999999....","...9aaaaaaaa9...","..999999999999..","..9aaaaaaaaaa9..",".99999999999999.",".9aaaaaaaaaaaa9.","9999999999999999","aaaaaaaaaaaaaaaa","................","................"]
    for y,row in enumerate(base):
        for x,ch in enumerate(row):
            if ch=='9': g[h-10+y,x]=9
            elif ch=='a': g[h-10+y,x]=10
    return g

# ---------------- font ----------------
CHARS=[chr(c) for c in range(32,128)]+[chr(c) for c in range(0x410,0x450)]+['Ё','ё','«','»','—','…','↑','↓','←','→']
def font_tiles():
    f=ImageFont.truetype(FONT_PIX,8)
    tiles=[]
    for ch in CHARS:
        im=Image.new('L',(8,8),0); d=ImageDraw.Draw(im)
        if ch=='…': d.point([(1,6),(3,6),(5,6)],fill=255)
        elif ch=='—': d.line([(0,3),(7,3)],fill=255)
        elif ch in '↑↓←→':
            arr=[(3,0),(2,1),(3,1),(4,1),(1,2),(2,2),(3,2),(4,2),(5,2),(2,3),(3,3),(4,3),(2,4),(3,4),(4,4),(2,5),(3,5),(4,5)]
            for (x,y) in arr:
                X,Y={'↑':(x,y),'↓':(x,6-y),'←':(y,x),'→':(6-y,x)}[ch]; d.point((X,Y),fill=255)
        else: d.text((0,0),ch,font=f,fill=255)
        a=np.array(im)>100
        g=np.zeros((8,8),dtype=np.uint8)
        for y in range(8):
            for x in range(8):
                if a[y,x]: g[y,x]=15
                elif (y>0 and x>0 and a[y-1,x-1]) or (x>0 and a[y,x-1]) or (y>0 and a[y-1,x]): g[y,x]=1
        tiles+=tiles_from(g,8,8)
    return tiles

# ---------------- title image (rescomp IMAGE, 16 colours) ----------------
def title_png():
    W,H=320,224
    canvas=Image.new('RGB',(W,H),(0,0,0))
    fr=Image.open(ART+'/frieze.jpg').convert('RGB').resize((320,180),Image.LANCZOS)
    # crop the battle (skip the generated borders, we draw our own meander)
    band=fr.crop((0,30,320,142))
    canvas.paste(band,(0,84))
    d=ImageDraw.Draw(canvas)
    # Greek line
    gf=ImageFont.truetype(FONT_SERIF,11)
    gtxt='ΒΑΤΡΑΧΟΜΥΟΜΑΧΙΑ'
    tw=d.textlength(gtxt,font=gf); d.text(((W-tw)/2,9),gtxt,font=gf,fill=(216,144,36))
    # main logo, gold with dark outline + shadow
    lf=ImageFont.truetype(FONT_SERIF,22)
    for i,line in enumerate(['ВОЙНА МЫШЕЙ','И ЛЯГУШЕК']):
        tw=d.textlength(line,font=lf); x=(W-tw)/2; y=22+i*25
        for dx,dy in ((2,2),(3,3)): d.text((x+dx,y+dy),line,font=lf,fill=(72,36,0))
        d.text((x,y),line,font=lf,fill=(252,216,36),stroke_width=1,stroke_fill=(144,72,0))
    # quantize to MD palette with 15 colours + black
    q=canvas.quantize(colors=15,method=Image.MEDIANCUT,dither=Image.NONE)
    pal=q.getpalette()[:45]
    cols=[]
    for i in range(15):
        r,g,b=pal[i*3:i*3+3]; cols.append((round(r/36)*36,round(g/36)*36,round(b/36)*36))
    idx=np.array(q)+1
    full=[(0,0,0)]+cols
    # meander bands (top row 0 and bottom rows 26-27 area under menu)
    m=meander_h()
    pidx={}  # map PAL0 meander colours into closest title palette entries
    def closest(c): return min(range(1,16),key=lambda i:sum((a-b)**2 for a,b in zip(full[i],c)))
    terr=closest(md(6,3,1)); blk=closest((0,0,0))
    for x in range(0,W,8):
        for yy in range(8):
            for xx in range(8):
                v=terr if m[yy,xx]==4 else 0
                idx[0+yy,x+xx]=v; idx[216+yy,x+xx]=v
                idx[76+yy,x+xx]=v; idx[196+yy,x+xx]=v
    # clear menu area (rows 198..215) to black and the area below frieze
    idx[204:216,:]=0
    img=Image.fromarray(idx.astype(np.uint8),'P')
    flat=[]
    for c in full: flat+=list(c)
    img.putpalette(flat+[0]*(768-len(flat)))
    img.save(ROOT+'/res/title.png')
    return img

def victory_banner(side):
    """big 64x64 king shield for result screens"""
    return piece(64,side,True)

def carr(name,tiles):
    s='const u32 %s[%d] = {\n'%(name,len(tiles)*8)
    for t in tiles: s+='  '+','.join('0x%08X'%v for v in t)+',\n'
    return s+'};\n'
def parr(name,p): return 'const u16 %s[16] = {'%name+','.join('0x%04X'%vdp(c) for c in p)+'};\n'

def main():
    os.makedirs(ROOT+'/res',exist_ok=True)
    h=['#ifndef GFX_DATA_H','#define GFX_DATA_H','#include <genesis.h>']
    c=['#include "gfx_data.h"']
    for n,p in (('pal_board',PAL0),('pal_pieces',PAL1),('pal_text',PAL2),('pal_gold',PAL3)):
        c.append(parr(n,p)); h.append('extern const u16 %s[16];'%n)
    groups=[]
    def add(name,tl):
        c.append(carr(name,tl)); h.append('extern const u32 %s[%d];'%(name,len(tl)*8)); h.append('#define %s_N %d'%(name.upper(),len(tl)))
    # pieces in column-major (works as 3x3 sprite and as plane tiles via base+x*3+y)
    pcs=[]; prev=[]
    for side in (0,1):
        for king in (0,1):
            g=piece(24,side,king); prev.append(g); pcs+=tiles_from(g,24,24,True)
    mk=marker(); prev.append(mk); pcs+=tiles_from(mk,24,24,True)
    for ph in range(3):
        b=burst(24,ph); prev.append(b); pcs+=tiles_from(b,24,24,True)
    add('tiles_pieces',pcs)   # 0..3 pieces, 4 marker, 5..7 burst  (9 tiles each)
    cur=[]; cprev=[]
    for col in (2,3,4):
        b=brackets(24,col); cprev.append(b); cur+=tiles_from(b,24,24,True)
    add('tiles_cursor',cur)   # 0 yellow (cursor), 1 green (selected), 2 red (error)
    brd=[]
    for g in (light_sq(),dark_sq(False),dark_sq(True)): brd+=tiles_from(g,24,24,False)
    add('tiles_board',brd)    # row-major 3x3: light, dark, dark-highlight
    fr=tiles_from(meander_h(),8,8)+tiles_from(meander_v(),8,8)+tiles_from(corner(),8,8)
    add('tiles_frame',fr)
    col=column(); add('tiles_column',tiles_from(col,16,26*8,False))   # 2 wide x 26 tall, row-major
    big=[]
    for side in (0,1): big+=tiles_from(victory_banner(side),64,64,False)
    add('tiles_big',big)      # 2 x 64 tiles, row-major 8x8
    add('tiles_font',font_tiles())
    h.append('#define FONT_CHARS %d'%len(CHARS))
    h.append('#endif')
    open(ROOT+'/src/gfx_data.h','w').write('\n'.join(h)+'\n')
    open(ROOT+'/src/gfx_data.c','w').write('\n'.join(c)+'\n')
    title_png()
    # previews
    def topng(g,pal,scale=4):
        im=Image.fromarray(g.astype(np.uint8),'P'); fl=[]
        for cc in pal: fl+=list(cc)
        im.putpalette(fl+[0]*(768-len(fl))); return im.convert('RGB').resize((g.shape[1]*scale,g.shape[0]*scale),Image.NEAREST)
    sheet=Image.new('RGB',(8*24*4+8*4,24*4*2+64*4),(40,40,40))
    for i,g in enumerate(prev): sheet.paste(topng(g,PAL1),((i%8)*100,0 if i<8 else 100))
    for i,s in enumerate((0,1)): sheet.paste(topng(victory_banner(s),PAL1),(i*270,200))
    os.makedirs(ROOT+'/docs',exist_ok=True); sheet.save(ROOT+'/docs/sprites_preview.png')
    print('ok', len(pcs), len(brd), len(CHARS))
main()
