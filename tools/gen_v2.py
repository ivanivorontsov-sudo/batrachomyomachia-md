#!/usr/bin/env python3
"""v2.0 graphics: pre-rendered ('volumetric') pieces, marble board, landscape with per-line sky
gradient (H-int), cloud parallax layer, gold logo, vase illustrations (rescomp IMAGE, compressed)."""
import os, math
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter, ImageEnhance
ROOT=os.path.dirname(os.path.abspath(__file__))+'/..'
ART=ROOT+'/tools/src_art2'
FONT_SERIF='/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf'
rng=np.random.default_rng(7)

def snap(c): return tuple(int(min(7,max(0,round(v/36))))*36 for v in c)
def vdp(c): r,g,b=c; return ((b//36)<<9)|((g//36)<<5)|((r//36)<<1)

def kmeans(px,k,fixed=(),iters=16):
    px=np.asarray(px,float).reshape(-1,3)
    if len(px)>60000: px=px[rng.choice(len(px),60000,replace=False)]
    fx=np.array(fixed,float).reshape(-1,3)
    lum=px@[.3,.59,.11]; order=np.argsort(lum)
    cent=px[order[np.linspace(0,len(px)-1,k).astype(int)]].copy()
    for _ in range(iters):
        allc=np.vstack([fx,cent]) if len(fx) else cent
        d=((px[:,None,:]-allc[None])**2).sum(2); a=d.argmin(1)
        for j in range(k):
            m=a==len(fx)+j
            if m.any(): cent[j]=px[m].mean(0)
            else: cent[j]=px[rng.integers(len(px))]
    out=[snap(c) for c in cent]
    # remove duplicates after snapping (keep count)
    return list(fixed)+out

def dither(rgb,mask,pal,allowed,strength=0.75,rowpal=None,serp=True):
    """Floyd-Steinberg; pal: list of rgb by index; rowpal(y)-> override dict idx->rgb"""
    H,W,_=rgb.shape; work=rgb.astype(float).copy(); out=np.zeros((H,W),np.uint8)
    for y in range(H):
        P=np.array([pal[i] for i in allowed],float)
        if rowpal:
            ov=rowpal(y)
            for j,i in enumerate(allowed):
                if i in ov: P[j]=ov[i]
        xs=range(W) if (not serp or y%2==0) else range(W-1,-1,-1); dx=1 if (not serp or y%2==0) else -1
        for x in xs:
            if not mask[y,x]: continue
            c=work[y,x]; j=((P-c)**2).sum(1).argmin(); out[y,x]=allowed[j]
            e=(c-P[j])*strength
            for (ox,oy,wt) in ((dx,0,7/16),(-dx,1,3/16),(0,1,5/16),(dx,1,1/16)):
                X,Y=x+ox,y+oy
                if 0<=X<W and Y<H and mask[Y,X]: work[Y,X]+=e*wt
    return out

def tiles_from(img,w,h,colmajor=False):
    out=[]; tx,ty=w//8,h//8
    order=[(x,y) for x in range(tx) for y in range(ty)] if colmajor else [(x,y) for y in range(ty) for x in range(tx)]
    for (x,y) in order:
        t=[]
        for r in range(8):
            v=0
            for c in range(8): v=(v<<4)|int(img[y*8+r][x*8+c])
            t.append(v)
        out.append(t)
    return out
def sprite_tiles(img):   # WxH multiple of 32: 32x32 blocks row-major, each column-major (4x4 sprite)
    h,w=img.shape; out=[]
    for by in range(0,h,32):
        for bx in range(0,w,32): out+=tiles_from(img[by:by+32,bx:bx+32],32,32,True)
    return out

class TileSet:
    def __init__(s,first_empty=True):
        s.tiles=[]; s.index={}
        if first_empty: s.add([0]*8)
    def add(s,t):
        k=tuple(t)
        if k not in s.index: s.index[k]=len(s.tiles); s.tiles.append(list(t))
        return s.index[k]
    def map(s,img,w,h):   # returns row-major map of tile indices
        return [s.add(t) for t in tiles_from(img,w,h)]

# ---------------- character renders ----------------
def keyed(name):
    im=Image.open(ART+'/'+name+'.jpg').convert('RGB'); a=np.array(im).astype(int)
    corner=a[4:20,4:20].reshape(-1,3).mean(0)
    d=np.sqrt(((a-corner)**2).sum(2))
    bg=(d<120)|((a[:,:,0]>150)&(a[:,:,1]<90)&(a[:,:,2]>90)&(a[:,:,2]>a[:,:,1]+80))
    alpha=Image.fromarray((~bg*255).astype(np.uint8)).filter(ImageFilter.MinFilter(5))
    # de-fringe: pull magenta out of edge pixels
    ys,xs=np.where(np.array(alpha)>0)
    box=(xs.min(),ys.min(),xs.max()+1,ys.max()+1)
    return im.crop(box), alpha.crop(box)

def render(name,size,sat=1.3,con=1.25,fit=None):
    im,al=keyed(name); w,h=im.size; f=fit or size-2; s=f/max(w,h)
    if size<=24:
        s=min((size+3)/h,(size-1)/w); cut=int(h*s)-(size-1)
        if cut>0: im=im.crop((0,int(cut/s),w,h)); al=al.crop((0,int(cut/s),w,h)); w,h=im.size
    nw,nh=max(1,round(w*s)),max(1,round(h*s))
    im=ImageEnhance.Color(im).enhance(sat); im=ImageEnhance.Contrast(im).enhance(con)
    im=im.resize((nw,nh),Image.LANCZOS).filter(ImageFilter.UnsharpMask(1,80,1))
    al=al.resize((nw,nh),Image.LANCZOS)
    rgb=np.zeros((size,size,3)); A=np.zeros((size,size))
    ox=(size-nw)//2; oy=size-1-nh
    rgb[oy:oy+nh,ox:ox+nw]=np.array(im); A[oy:oy+nh,ox:ox+nw]=np.array(al)/255
    return rgb,A>0.5

def outline(idx,mask,col):
    H,W=mask.shape; o=idx.copy()
    for y in range(H):
        for x in range(W):
            if mask[y,x]: continue
            for dy,dx in ((0,1),(1,0),(0,-1),(-1,0)):
                Y,X=y+dy,x+dx
                if 0<=Y<H and 0<=X<W and mask[Y,X]: o[y,x]=col; break
    return o

def breathe(g):   # idle frame: upper body lifted 1px
    h=g.shape[0]; m=int(h*0.55); o=g.copy(); o[0:m-1]=g[1:m]; o[m-1]=g[m-1]; return o

def build_side(names,fixed,ncol,outline_rgb,tail=()):
    """names: list of (name,size). returns palette, list of idx images"""
    rs=[render(n,s) for n,s in names]
    px=np.vstack([r[m] for r,m in rs])
    pal=kmeans(px,ncol,list(fixed)+list(tail))
    nf=len(fixed); nt=len(tail)
    pal=[(0,0,0)]+pal[:nf]+pal[nf+nt:]+pal[nf:nf+nt]
    oc=min(range(1,16),key=lambda i:sum((a-b)**2 for a,b in zip(pal[i],outline_rgb)))
    outs=[]
    for (r,m),(n,s) in zip(rs,names):
        g=dither(r,m,pal,list(range(1,16)),0.55 if s<=24 else 0.8)
        outs.append(outline(g,m,oc))
    return pal,outs

def preview(g,pal,scale):
    im=Image.fromarray(g.astype(np.uint8),'P'); fl=[]
    for c in pal: fl+=list(c)
    im.putpalette(fl+[0]*(768-len(fl))); return im.convert('RGB').resize((g.shape[1]*scale,g.shape[0]*scale),Image.NEAREST)

# ---------------- board / backdrop (PAL0) ----------------
def noise(h,w,seed,scale=6.0,oct=4):
    r=np.random.default_rng(seed); acc=np.zeros((h,w))
    for o in range(oct):
        s=max(1,int(scale/(2**o))+1); g=r.random((h//s+3,w//s+3))
        im=Image.fromarray((g*255).astype(np.uint8)).resize(((w//s+3)*s,(h//s+3)*s),Image.BICUBIC)
        acc+=np.array(im)[:h,:w]/255/(2**o)
    return acc/acc.max()
def marble(h,w,seed,base,vein,dark=False):
    n=noise(h,w,seed,8,4); yy,xx=np.mgrid[0:h,0:w]
    v=np.abs(np.sin((xx*0.18+yy*0.11)+n*6.0))
    veins=(v<0.18).astype(float)*(1-v/0.18)
    c=np.array(base,float)[None,None]*(0.88+0.24*n[...,None])
    c=c*(1-veins[...,None]*0.6)+np.array(vein,float)[None,None]*veins[...,None]*0.6
    return c
def bevel(c,lt=1.35,dk=0.55):
    c=c.copy(); c[0,:]*=lt; c[:,0]*=lt; c[1,1:-1]*=1.12; c[-1,:]*=dk; c[:,-1]*=dk; c[-2,1:-1]*=0.8
    return np.clip(c,0,255)
def square(kind,var):
    if kind=='L': c=marble(24,24,10+var,(232,220,192),(150,130,110))
    elif kind=='D': c=marble(24,24,30+var,(54,78,64),(120,150,120))
    else: c=marble(24,24,50,(140,96,40),(230,180,80))
    # spherical-ish lighting from top-left
    yy,xx=np.mgrid[0:24,0:24]; l=1.08-((xx+yy)/46.0)*0.22
    return bevel(c*l[...,None])

def column_img(h):
    w=16; c=np.zeros((h,w,3)); xs=np.arange(w)
    shade=0.55+0.6*np.cos((xs-5)/w*math.pi)**2
    base=np.array([220,206,176])
    for y in range(h):
        flute=np.where((xs%4)==3,0.78,1.0)
        c[y]=base*shade[:,None]*flute[:,None]
    for y0 in (0,h-12):   # capital / base
        for y in range(y0,y0+12):
            k=1.0 if (y-y0) in (0,1,10,11) else 0.85
            c[y]=np.array([230,210,150])*k*(0.7+0.4*np.cos((xs-5)/w*math.pi)**2)[:,None]
    return np.clip(c,0,255)

def meander_strip(w,h,col_hi,col_lo,bg):
    c=np.zeros((h,w,3))+np.array(bg,float)
    pat=["########","#......#","#.####.#","#.#..#.#","#.#.##.#","#.#....#","#.######","........"]
    for y in range(h):
        for x in range(w):
            if pat[y%8][x%8]=='#':
                c[y,x]=col_hi if (y%8)<4 else col_lo
    return c

def game_backdrop():
    W,H=320,224
    c=marble(H,W,99,(40,34,30),(70,60,50))*0.9   # dark stone
    # board area squares
    sqs={}
    for kind in 'LD':
        for v in (0,1): sqs[(kind,v)]=square(kind,v)
    sqs[('H',0)]=square('H',0)
    for r in range(8):
        for col in range(8):
            dark=(r+col)&1; v=(r*3+col*5)>>1&1
            c[16+r*24:16+r*24+24,24+col*24:24+col*24+24]=sqs[('D' if dark else 'L',v)]
    # gold frame (8px) around board: x 16..224, y 8..216
    gold=meander_strip(208+16,8,(250,214,90),(190,130,40),(70,40,10))
    c[8:16,16:240-16]=gold[:, :208]
    c[208:216,16:224]=gold[:, :208][::-1]
    v=meander_strip(8,192,(250,214,90),(190,130,40),(70,40,10))
    c[16:208,16:24]=v; c[16:208,216:224]=v[:,::-1]
    for (x,y) in ((16,8),(216,8),(16,208),(216,208)): c[y:y+8,x:x+8]=[[250,214,90]]*8; c[y+2:y+6,x+2:x+6]=(120,60,10)
    # column on the left (cols 0..1, rows 1..26)
    c[8:216,0:16]=column_img(208)
    # friezes top/bottom
    fr=meander_strip(W,8,(220,120,40),(170,80,20),(20,10,5))
    c[0:8]=fr; c[216:224]=fr
    # panel: carved tablet with border
    p=c[8:216,224:320]; p*=0.8
    p[0:2,:]=(200,160,70); p[-2:,:]=(120,80,20); p[:,0:2]=(200,160,70); p[:,-2:]=(120,80,20)
    sq_mask=np.zeros((H,W),bool); sq_mask[16:208,24:216]=True
    return np.clip(c,0,255), sqs

def build_game_bg():
    rgbimg,sqs=game_backdrop()
    pal=[(0,0,0)]+kmeans(rgbimg,15)
    idx=dither(rgbimg,np.ones(rgbimg.shape[:2],bool),pal,list(range(1,16)),0.5)
    ts=TileSet()
    # canonical squares: take from the composed board (variant order L0,L1,D0,D1,H)
    sqt=[]
    def sq_tiles(g): return tiles_from(g,24,24,False)
    found={}
    for r in range(8):
        for col in range(8):
            dark=(r+col)&1; v=(r*3+col*5)>>1&1; key=('D' if dark else 'L',v)
            if key not in found: found[key]=idx[16+r*24:16+r*24+24,24+col*24:24+col*24+24]
    hl=dither(sqs[('H',0)],np.ones((24,24),bool),pal,list(range(1,16)),0.5)
    for key in (('L',0),('L',1),('D',0),('D',1)): sqt+=sq_tiles(found[key])
    sqt+=sq_tiles(hl)
    bg=idx.copy(); bg[16:208,24:216]=0
    m=ts.map(bg,320,224)
    for y in range(2,26):
        for x in range(3,27): m[y*40+x]=0xFFFF
    return pal,sqt,ts.tiles,m,idx

# ---------------- landscape with H-int sky gradient ----------------
SKY=1; BAND=4
def build_landscape():
    im=Image.open(ART+'/landscape.jpg').convert('RGB')
    im=ImageEnhance.Color(im).enhance(1.15)
    im=im.resize((398,224),Image.LANCZOS).crop((66,0,386,224))
    a=np.array(im).astype(float); H,W=224,320
    med=np.array([np.median(a[y,0:150],axis=0) for y in range(H)])
    d=np.sqrt(((a-med[:,None,:])**2).sum(2))
    sky=(d<22)&(np.arange(H)[:,None]<108)
    sky[60:110,190:]&=d[60:110,190:]<10
    nb=H//BAND; raw=[]
    for b in range(nb):
        m=sky[b*BAND:(b+1)*BAND]
        raw.append(a[b*BAND:(b+1)*BAND][m].mean(0) if m.sum()>8 else None)
    last=raw[0]
    for i in range(nb):
        if raw[i] is None: raw[i]=last
        last=raw[i]
    raw=np.array(raw); sm=raw.copy()
    for i in range(nb): sm[i]=raw[max(0,i-2):i+3].mean(0)
    lo=[];hi=[];fr=[]
    for c in sm:
        f=np.clip(c/36,0,7); l=np.floor(f); t=f-l; h=np.minimum(l+(t>0.01),7)
        lo.append(tuple(int(v)*36 for v in l)); hi.append(tuple(int(v)*36 for v in h)); fr.append(float(t[t>0.01].mean()) if (t>0.01).any() else 0.0)
    pal=[(0,0,0),lo[0],hi[0]]+kmeans(a[~sky],13)
    rowpal=lambda y:{SKY:lo[y//BAND],SKY+1:hi[y//BAND]}
    allowed=list(range(1,16))
    idx=dither(a,np.ones((H,W),bool),pal,allowed,0.6,rowpal)
    B4=np.array([[0,8,2,10],[12,4,14,6],[3,11,1,9],[15,7,13,5]])/16+1/32
    yy,xx=np.mgrid[0:H,0:W]
    frac=np.array([fr[y//BAND] for y in range(H)])[:,None]
    dith=np.where(B4[yy%4,xx%4]<frac,SKY+1,SKY)
    idx[sky]=dith[sky]
    grad=list(zip(lo,hi))
    return pal,grad,idx

def clouds_layer(pal_land,grad):
    W,H=256,40
    c=np.zeros((H,W,3)); A=np.zeros((H,W))
    r=np.random.default_rng(3)
    for i in range(9):
        cx=r.integers(0,W); cy=r.integers(14,30); s=r.integers(14,28)
        for k in range(7):
            bx=cx+r.integers(-s,s); by=cy+r.integers(-4,4); rad=r.integers(5,11)
            yy,xx=np.mgrid[0:H,0:W]
            for wrap in (-W,0,W):
                dd=((xx-bx-wrap)**2+((yy-by)*1.6)**2)/rad**2
                A=np.maximum(A,np.clip(1.2-dd,0,1))
    yy=np.mgrid[0:H,0:W][0]
    shade=np.clip(1.1-(yy-10)/40,0.6,1.1)
    n=noise(H,W,5,6,3)
    col=np.array([255,240,220])[None,None]*shade[...,None]*(0.9+0.15*n[...,None])
    col[...,2]+= (1-shade)*60
    mask=A>0.55
    allowed=[i for i in range(3,16)]
    g=dither(np.clip(col,0,255),mask,pal_land,allowed,0.5)
    return g

def build_logo():
    W,H=320,56
    canvas=Image.new('RGB',(W,H),(0,0,0)); d=ImageDraw.Draw(canvas)
    # dark plate with gold border
    d.rectangle((8,0,311,55),fill=(28,14,6)); d.rectangle((8,0,311,1),fill=(230,180,60)); d.rectangle((8,54,311,55),fill=(150,90,20))
    d.rectangle((8,3,311,3),fill=(150,90,20)); d.rectangle((8,52,311,52),fill=(230,180,60))
    gf=ImageFont.truetype(FONT_SERIF,9); t='ΒΑΤΡΑΧΟΜΥΟΜΑΧΙΑ'; tw=d.textlength(t,font=gf)
    lf=ImageFont.truetype(FONT_SERIF,19)
    layer=Image.new('L',(W,H),0); ld=ImageDraw.Draw(layer)
    for i,line in enumerate(['ВОЙНА МЫШЕЙ','И ЛЯГУШЕК']):
        tw2=ld.textlength(line,font=lf); x=(W-tw2)/2; y=5+i*22
        d.text((x+2,y+2),line,font=lf,fill=(10,4,0))
        ld.text((x,y),line,font=lf,fill=255)
    m=np.array(layer)>100; yy=np.mgrid[0:H,0:W][0].astype(float)
    # metallic gradient per line of text
    ph=((yy-5)%22)/22.0
    gold=np.stack([255-ph*60, 230-ph*150, 120-ph*110],-1)
    gold[(ph<0.18)]=(255,250,210)
    arr=np.array(canvas).astype(float); arr[m]=gold[m]
    # bevel: darker bottom-right edge of letters
    edge=m & ~np.roll(np.roll(m,1,0),1,1); arr[edge]=arr[edge]*0.95
    arr[m & ~np.roll(m,-1,0)]*=0.55
    plate=np.zeros((H,W),bool); plate[0:56,8:312]=True
    fixed=[(36,0,0),]   # idx1 = text shadow (dark)
    pal=[(0,0,0)]+fixed+kmeans(arr[plate],12)+[(252,252,252),(252,216,36)]  # 14 white (unused on title S/H as plane colour), 15 gold text
    idx=dither(arr,plate,pal,list(range(1,14))+[15],0.4)
    return pal,idx

def glint():
    g=np.zeros((56,16),np.uint8)
    for y in range(56):
        x0=int(10-y*0.18)
        for x in range(16):
            if x0<=x<x0+4: g[y,x]=14
    return g

# ---------------- vase illustrations (rescomp IMAGE) ----------------
def vase(name,out,size=(256,144)):
    im=Image.open(ART+'/'+name+'.jpg').convert('RGB').resize(size,Image.LANCZOS)
    im=ImageEnhance.Contrast(im).enhance(1.1)
    a=np.array(im).astype(float)
    pal=[(0,0,0)]+kmeans(a,15)
    idx=dither(a,np.ones(a.shape[:2],bool),pal,list(range(1,16)),0.45)
    img=Image.fromarray(idx,'P'); fl=[]
    for c in pal: fl+=list(c)
    img.putpalette(fl+[0]*(768-len(fl))); img.save(out)

def carr(name,tiles):
    s='const u32 %s[%d] = {\n'%(name,max(1,len(tiles)*8))
    for t in tiles: s+='  '+','.join('0x%08X'%v for v in t)+',\n'
    return s+'};\n'
def parr(name,p): return 'const u16 %s[16] = {'%name+','.join('0x%04X'%vdp(c) for c in (list(p)+[(0,0,0)]*16)[:16])+'};\n'
def marr(name,m,typ='u16'): return 'const %s %s[%d] = {'%(typ,name,len(m))+','.join(str(v) for v in m)+'};\n'

def main():
    H=['#ifndef GFX_V2_H','#define GFX_V2_H','#include <genesis.h>']; C=['#include "gfx_v2.h"']
    def add_t(n,t): C.append(carr(n,t)); H.append('extern const u32 %s[];'%n); H.append('#define %s_N %d'%(n.upper(),len(t)))
    def add_p(n,p): C.append(parr(n,p)); H.append('extern const u16 %s[16];'%n)
    def add_m(n,m): C.append(marr(n,m)); H.append('extern const u16 %s[%d];'%(n,len(m)))
    shots=[]
    # mice: PAL1 (15 free colours); frogs: PAL2 with idx1 black + idx15 cream (shared with text)
    mp,mi=build_side([('mouse',24),('mouse_king',24),('mouse',64),('mouse_king',64)],[(0,0,0)],14,(0,0,0))
    fp,fi=build_side([('frog',24),('frog_king',24),('frog',64),('frog_king',64)],[(0,0,0)],13,(0,0,0),[(252,216,144)])
    add_p('pal2_mice',mp); add_p('pal2_frogs',fp)
    gold=[(0,0,0),(36,0,0),(252,252,0),(144,252,36),(252,72,0),(252,180,0),(108,72,0),(180,108,0),(216,144,36),(252,216,108),(72,36,0),(252,252,180),(144,108,36),(216,180,72),(252,252,252),(252,216,36)]
    add_p('pal2_gold',gold)
    # pieces 24x24: frame A and B. markers / bursts in gold palette
    pa=[];pb=[]
    for g in (mi[0],mi[1],fi[0],fi[1]): pa+=tiles_from(g,24,24,True); pb+=tiles_from(breathe(g),24,24,True)
    # marker: glowing orb
    mk=np.zeros((24,24),np.uint8); yy,xx=np.mgrid[0:24,0:24]; rr=np.sqrt((xx-11.5)**2+(yy-11.5)**2)
    mk[rr<6]=7; mk[rr<4.5]=8; mk[rr<3]=9; mk[rr<1.6]=11; mk[(rr>=6)&(rr<7)]=10
    pa+=tiles_from(mk,24,24,True)
    for ph in range(3):   # dust burst
        b=np.zeros((24,24),np.uint8); r2=np.random.default_rng(ph)
        for k in range(26+ph*8):
            ang=r2.random()*6.28; dist=(ph+1)*3.2*r2.random()+2
            x=int(11.5+math.cos(ang)*dist); y=int(11.5+math.sin(ang)*dist*0.8)
            for dy in range(-1,2-ph//2):
                for dx in range(-1,2-ph//2):
                    if 0<=x+dx<24 and 0<=y+dy<24: b[y+dy,x+dx]=[11,9,13][ph] if (dx+dy)%2 else [9,13,12][ph]
        pa+=tiles_from(b,24,24,True)
    add_t('tiles2_pieces',pa); add_t('tiles2_pieces_b',pb)
    # big 64x64: mice king, frog king, mouse warrior, frog warrior (sprite order: 4 blocks 32x32)
    big=[]
    for g in (mi[3],fi[3],mi[2],fi[2]): big+=sprite_tiles(g)
    add_t('tiles2_big',big)
    # cursor (PAL3): brackets + highlight fill (idx14 = highlight operator in S/H mode)
    cur=[]
    for col in (2,3,4):
        g=np.zeros((24,24),np.uint8); g[2:22,2:22]=14
        for (x0,y0,sx,sy) in ((0,0,1,1),(23,0,-1,1),(0,23,1,-1),(23,23,-1,-1)):
            for i in range(7):
                for t in range(2):
                    g[y0+sy*t,x0+sx*i]=col; g[y0+sy*i,x0+sx*t]=col
        cur+=tiles_from(g,24,24,True)
    add_t('tiles2_cursor',cur)
    # drop shadow 24x8 (idx15 = shadow operator) and particles (dust PAL1 colours / spark gold)
    sh=np.zeros((8,24),np.uint8); yy,xx=np.mgrid[0:8,0:24]
    sh[((xx-11.5)/11)**2+((yy-3.5)/3.5)**2<1]=15
    misc=tiles_from(sh,24,8,True)
    p1=np.zeros((8,8),np.uint8); p1[2:6,3:5]=9; p1[3:5,2:6]=9; p1[3,3]=11
    p2=np.zeros((8,8),np.uint8); p2[3:5,3:5]=11; p2[2,3]=9; p2[5,4]=9
    misc+=tiles_from(p1,8,8)+tiles_from(p2,8,8)
    gl=glint(); misc+=tiles_from(gl[0:32],16,32,True)+tiles_from(gl[32:56],16,24,True)
    add_t('tiles2_misc',misc)   # 0..2 shadow, 3,4 sparks, 5..12 glint top (2x4), 13..18 glint bottom (2x3)
    # game backdrop
    bp,sqt,bgt,bgm,gameprev=build_game_bg()
    add_p('pal2_board',bp); add_t('tiles2_squares',sqt); add_t('tiles2_bg',bgt); add_m('map2_bg',bgm)
    # landscape + sky gradient
    lp,grad,lidx=build_landscape()
    lt=TileSet(); lm=lt.map(lidx,320,224)
    cl=clouds_layer(lp,grad); ct=TileSet(); cm=ct.map(cl,256,40)
    add_p('pal2_land',lp); add_t('tiles2_land',lt.tiles); add_m('map2_land',lm)
    add_t('tiles2_clouds',ct.tiles); add_m('map2_clouds',cm)
    C.append('const u16 sky_grad[%d] = {'%(2*len(grad))+','.join('0x%04X,0x%04X'%(vdp(a),vdp(b)) for a,b in grad)+'};'); H.append('extern const u16 sky_grad[%d];'%(2*len(grad))); H.append('#define SKY_BANDS %d'%len(grad))
    lg_pal,lg=build_logo(); lgt=TileSet(); lgm=lgt.map(lg,320,56)
    add_p('pal2_logo',lg_pal); add_t('tiles2_logo',lgt.tiles); add_m('map2_logo',lgm)
    H.append('#endif')
    open(ROOT+'/src/gfx_v2.h','w').write('\n'.join(H)+'\n'); open(ROOT+'/src/gfx_v2.c','w').write('\n'.join(C)+'\n')
    for n in ('intro1','intro2','intro3','intro4','intro5'): vase(n,ROOT+'/res/v2/%s.png'%n)
    for n in ('end_mice','end_frogs','end_draw'): vase(n,ROOT+'/res/v2/%s.png'%n,(256,128))
    # previews
    os.makedirs(ROOT+'/docs',exist_ok=True)
    sheet=Image.new('RGB',(4*64*3+40,64*3*2+24*6+20),(60,60,60))
    for i,g in enumerate((mi[0],mi[1],fi[0],fi[1])): sheet.paste(preview(g,mp if i<2 else fp,6),(i*150,0))
    for i,g in enumerate((mi[3],fi[3],mi[2],fi[2])): sheet.paste(preview(g,mp if i%2==0 else fp,3),(i*200,160))
    sheet.save(ROOT+'/docs/v2_pieces_preview.png')
    preview(gameprev,bp,2).save(ROOT+'/docs/v2_board_preview.png')
    lv=lidx.copy(); prev=np.zeros((224,320,3),np.uint8)
    for y in range(224):
        P=list(lp); P[SKY],P[SKY+1]=grad[y//BAND]
        prev[y]=np.array(P,np.uint8)[lv[y]]
    Image.fromarray(prev).resize((640,448),Image.NEAREST).save(ROOT+'/docs/v2_land_preview.png')
    preview(lg,lg_pal,2).save(ROOT+'/docs/v2_logo_preview.png')
    print('land tiles',len(lt.tiles),'clouds',len(ct.tiles),'logo',len(lgt.tiles),'bg',len(bgt),'big',len(big))
main()
