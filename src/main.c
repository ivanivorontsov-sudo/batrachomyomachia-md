/* БАТРАХОМИОМАХИЯ — Война мышей и лягушек. Шашки для Sega Mega Drive / Genesis (SGDK). */
#include <genesis.h>
#include "rules.h"
#include "ai.h"
#include "gfx_data.h"
#include "gfx_v2.h"
#include "resources.h"

/* ---------------- VRAM layout ---------------- */
#define T_FONT    (TILE_USER_INDEX)
#define T_DYN     (T_FONT + FONT_CHARS)
#define T_BOARD   (T_DYN)
#define T_FRAME   (T_BOARD + TILES_BOARD_N)
#define T_COLUMN  (T_FRAME + TILES_FRAME_N)
#define T_PIECES  (T_COLUMN + TILES_COLUMN_N)
#define T_CURSOR  (T_PIECES + TILES_PIECES_N)
#define T_BIG     (T_CURSOR + TILES_CURSOR_N)
#define T_MISC1   (T_BIG + TILES_BIG_N)          /* simple style: particles */
/* volumetric game layout */
#define T2_SQ     (T_DYN)
#define T2_BG     (T2_SQ + TILES2_SQUARES_N)
#define T2_PIECES (T2_BG + TILES2_BG_N)
#define T2_CURSOR (T2_PIECES + TILES2_PIECES_N)
#define T2_MISC   (T2_CURSOR + TILES2_CURSOR_N)
#define T2_END    (T2_MISC + TILES2_MISC_N)
/* title / menu layout */
#define TT_LAND   (T_DYN)
#define TT_CLOUDS (TT_LAND + TILES2_LAND_N)
#define TT_LOGO   (TT_CLOUDS + TILES2_CLOUDS_N)
#define TT_BIG    (TT_LOGO + TILES2_LOGO_N)
#define TT_MISC   (TT_BIG + TILES2_BIG_N)
#define TT_SOLID  (TT_MISC + TILES2_MISC_N)
#define TT_HL     (TT_SOLID + 2)
#define TT_END    (TT_HL + 4)
_Static_assert(TT_END <= 1536, "title tiles overflow into the plane maps");
_Static_assert(T2_END <= 1536, "game tiles overflow");

#define BX 3   /* board origin in tiles */
#define BY 2
#define PX 28  /* panel */

enum { ST_TITLE, ST_MENU, ST_INTRO, ST_GAME, ST_END };
enum { PK_MOUSE=0, PK_MOUSE_K, PK_FROG, PK_FROG_K, PK_MARK, PK_BURST0 };

/* ---------------- settings & state ---------------- */
static u8 opt_players = 1;      /* 1 or 2 */
static u8 opt_side = SIDE_MICE; /* human side in 1P */
static u8 opt_level = 1;
static u8 opt_rules = RULES_RUSSIAN;
static u8 intro_seen = FALSE;
static u8 opt_gfx = 0;          /* 0 = "Объёмная" (pre-rendered, S/H), 1 = "Простая" (v1 vase look) */
#define VOL (opt_gfx==0)
static u16 t_pieces, t_cursor, t_misc;   /* current VRAM bases */
static u16 text_prio = 1;

static Pos pos;
static Move legal[MAX_MOVES];
static int nlegal;
static u8 flip;
#define HIST 300
static Pos hist[HIST];
static s16 hist_from[HIST], hist_to[HIST];
static int nhist;
static s16 last_from=-1, last_to=-1;
static s16 cur_r=5, cur_c=0;     /* cursor in display coords */
static s16 sel=-1;               /* selected square */
static u8 prefix[MAX_PATH]; static u8 nprefix;
static int result;               /* -1 none, 0 mice won, 1 frogs won, 2 draw */

/* ---------------- input ---------------- */
static u16 pad_now, pad_prev, pad_press, rep_cnt;
static void input_update(void){
    pad_prev = pad_now;
    pad_now = JOY_readJoypad(JOY_1) | JOY_readJoypad(JOY_2);
    pad_press = pad_now & ~pad_prev;
    u16 dirs = pad_now & (BUTTON_UP|BUTTON_DOWN|BUTTON_LEFT|BUTTON_RIGHT);
    if (dirs && (dirs == (pad_prev & (BUTTON_UP|BUTTON_DOWN|BUTTON_LEFT|BUTTON_RIGHT)))) {
        if (++rep_cnt >= 14) { pad_press |= dirs; rep_cnt = 10; }
    } else rep_cnt = 0;
}
static void anim_tick(void);
static vu16 anim_t;
static vu8 ai_busy;
static void frame(void){ SYS_doVBlankProcess(); input_update(); anim_tick(); }
static void wait_frames(u16 n){ while(n--) frame(); }

/* ---------------- sound ---------------- */
static void sfx(const u8 *s, u32 len){ XGM2_playPCM(s, len, SOUND_PCM_CH2); }
#define SFX(x) sfx(x, sizeof(x))
static s16 cur_track=-1;
static void music(s16 t){ if(t==cur_track) return; cur_track=t; XGM2_playTrack(t); }

/* ---------------- text ---------------- */
static u16 glyph(u16 cp){
    if (cp>=32 && cp<128) return cp-32;
    if (cp>=0x410 && cp<0x450) return 96+(cp-0x410);
    switch(cp){ case 0x401: return 160; case 0x451: return 161; case 0xAB: return 162; case 0xBB: return 163;
                case 0x2014: return 164; case 0x2026: return 165;
                case 0x2191: return 166; case 0x2193: return 167; case 0x2190: return 168; case 0x2192: return 169; }
    return '?'-32;
}
static u16 utf8_len(const char *s){ u16 n=0; while(*s){ if((*s & 0xC0)!=0x80) n++; s++; } return n; }
static void text(const char *s, u16 x, u16 y, u16 pal){
    const u8 *p=(const u8*)s;
    while(*p){
        u16 cp;
        if(*p<0x80) cp=*p++;
        else if((*p&0xE0)==0xC0){ cp=((p[0]&0x1F)<<6)|(p[1]&0x3F); p+=2; }
        else { cp=((p[0]&0x0F)<<12)|((p[1]&0x3F)<<6)|(p[2]&0x3F); p+=3; }
        VDP_setTileMapXY(BG_A, TILE_ATTR_FULL(pal,text_prio,0,0,T_FONT+glyph(cp)), x++, y);
    }
}
static void text_c(const char *s, u16 x0, u16 w, u16 y, u16 pal){
    u16 n=utf8_len(s); text(s, x0 + (n<w ? (w-n)/2 : 0), y, pal);
}
static void clear_a(u16 x,u16 y,u16 w,u16 h){ VDP_fillTileMapRect(BG_A, 0, x,y,w,h); }
static void num(char *b, int v){ char t[8]; int n=0; if(v==0) t[n++]='0'; while(v){ t[n++]='0'+v%10; v/=10; } while(n) *b++=t[--n]; *b=0; }

/* ---------------- palettes ---------------- */
static u16 pal_all[64];
static void build_pal(const u16 *p0){
    memcpy(pal_all, p0, 32); memcpy(pal_all+16, pal_pieces, 32);
    memcpy(pal_all+32, pal_text, 32); memcpy(pal_all+48, pal_gold, 32);
}
static void hint_stop(void);
static void fade_out(void){ hint_stop(); PAL_fadeOutAll(16, FALSE); }
static void fade_in(void){ PAL_fadeInAll(pal_all, 16, FALSE); }

static void reset_screen(void){
    VDP_setEnable(FALSE);
    VDP_clearPlane(BG_A, TRUE); VDP_clearPlane(BG_B, TRUE);
    VDP_resetSprites(); VDP_updateSprites(1, DMA);
    VDP_setHilightShadow(FALSE);
    VDP_setScrollingMode(HSCROLL_PLANE, VSCROLL_PLANE);
    VDP_setHorizontalScroll(BG_A,0); VDP_setHorizontalScroll(BG_B,0);
    VDP_setVerticalScroll(BG_A,0); VDP_setVerticalScroll(BG_B,0);
    text_prio=1;
    VDP_setEnable(TRUE);
}
static void load_game_tiles(void){
    VDP_loadTileData(tiles_board, T_BOARD, TILES_BOARD_N, DMA);
    VDP_loadTileData(tiles_frame, T_FRAME, TILES_FRAME_N, DMA);
    VDP_loadTileData(tiles_column, T_COLUMN, TILES_COLUMN_N, DMA);
    VDP_loadTileData(tiles_pieces, T_PIECES, TILES_PIECES_N, DMA);
    VDP_loadTileData(tiles_cursor, T_CURSOR, TILES_CURSOR_N, DMA);
    VDP_loadTileData(tiles_big, T_BIG, TILES_BIG_N, DMA);
    VDP_loadTileData(tiles2_misc, T_MISC1, TILES2_MISC_N, DMA);
    t_pieces=T_PIECES; t_cursor=T_CURSOR; t_misc=T_MISC1;
}
static void load_game_tiles2(void){
    VDP_loadTileData(tiles2_squares, T2_SQ, TILES2_SQUARES_N, DMA);
    VDP_loadTileData(tiles2_bg, T2_BG, TILES2_BG_N, DMA);
    VDP_loadTileData(tiles2_pieces, T2_PIECES, TILES2_PIECES_N, DMA);
    VDP_loadTileData(tiles2_cursor, T2_CURSOR, TILES2_CURSOR_N, DMA);
    VDP_loadTileData(tiles2_misc, T2_MISC, TILES2_MISC_N, DMA);
    t_pieces=T2_PIECES; t_cursor=T2_CURSOR; t_misc=T2_MISC;
}
#define MH TILE_ATTR_FULL(PAL0,0,0,0,T_FRAME)
#define MV TILE_ATTR_FULL(PAL0,0,0,0,T_FRAME+1)
#define MC TILE_ATTR_FULL(PAL0,0,0,0,T_FRAME+2)
static void friezes(void){
    VDP_fillTileMapRect(BG_B, MH, 0,0,40,1);
    VDP_fillTileMapRect(BG_B, MH, 0,27,40,1);
}
static void big_emblem(int side, u16 x, u16 y){
    for(u16 j=0;j<8;j++) for(u16 i=0;i<8;i++)
        VDP_setTileMapXY(BG_A, TILE_ATTR_FULL(PAL1,0,0,0,T_BIG+side*64+j*8+i), x+i, y+j);
}

/* ---------------- board rendering ---------------- */
static void sq_disp(int sq, int *dr, int *dc){
    int r=sq_row(sq), c=sq_col(sq);
    if(flip){ r=7-r; c=7-c; }
    *dr=r; *dc=c;
}
static int disp_sq(int dr,int dc){ return flip ? rc_to_sq(7-dr,7-dc) : rc_to_sq(dr,dc); }
static void draw_square_b(int dr,int dc,int hl){
    int dark=(dr+dc)&1; u16 base;
    if(VOL){ int v=((dr*3+dc*5)>>1)&1; base=T2_SQ + (dark ? (hl?36:(2+v)*9) : v*9); }
    else base=T_BOARD + (dark ? (hl?18:9) : 0);
    for(int ty=0;ty<3;ty++) for(int tx=0;tx<3;tx++)
        VDP_setTileMapXY(BG_B, TILE_ATTR_FULL(PAL0,1,0,0,base+ty*3+tx), BX+dc*3+tx, BY+dr*3+ty);
}
static u16 kpal(int k){ if(!VOL) return PAL1; return k<2 ? PAL1 : k<4 ? PAL2 : PAL3; }
static void put_glyph9_pal(int dr,int dc,int k,u16 pal){
    for(int tx=0;tx<3;tx++) for(int ty=0;ty<3;ty++)
        VDP_setTileMapXY(BG_A, k<0 ? 0 : TILE_ATTR_FULL(pal,1,0,0,t_pieces+k*9+tx*3+ty), BX+dc*3+tx, BY+dr*3+ty);
}
static void put_glyph9(int dr,int dc,int k){ put_glyph9_pal(dr,dc,k,kpal(k)); }
static int piece_at(const Pos *p,int sq){
    bb_t b=BIT(sq);
    if(p->men[0]&b) return PK_MOUSE; if(p->kings[0]&b) return PK_MOUSE_K;
    if(p->men[1]&b) return PK_FROG;  if(p->kings[1]&b) return PK_FROG_K;
    return -1;
}
static void draw_piece_sq(int sq){ int dr,dc; sq_disp(sq,&dr,&dc); put_glyph9(dr,dc,piece_at(&pos,sq)); }
static void draw_pieces(void){ for(int s=0;s<32;s++) draw_piece_sq(s); }
static void draw_board(void){
    for(int r=0;r<8;r++) for(int c=0;c<8;c++){
        int sq=disp_sq(r,c); draw_square_b(r,c, sq>=0 && (sq==last_from||sq==last_to));
    }
}
static void draw_frame_static(void){
    if(VOL){
        for(u16 y=0;y<28;y++) for(u16 x=0;x<40;x++){ u16 m=map2_bg[y*40+x]; if(m!=0xFFFF) VDP_setTileMapXY(BG_B, TILE_ATTR_FULL(PAL0,1,0,0,T2_BG+m), x, y); }
        return;
    }
    friezes();
    for(u16 y=0;y<26;y++) for(u16 x=0;x<2;x++)
        VDP_setTileMapXY(BG_B, TILE_ATTR_FULL(PAL0,0,0,0,T_COLUMN+y*2+x), x, 1+y);
    VDP_fillTileMapRect(BG_B, MH, 3,1,24,1); VDP_fillTileMapRect(BG_B, MH, 3,26,24,1);
    VDP_fillTileMapRect(BG_B, MV, 2,2,1,24); VDP_fillTileMapRect(BG_B, MV, 27,2,1,24);
    VDP_setTileMapXY(BG_B,MC,2,1); VDP_setTileMapXY(BG_B,MC,27,1); VDP_setTileMapXY(BG_B,MC,2,26); VDP_setTileMapXY(BG_B,MC,27,26);
}

/* ---------------- sprites ---------------- */
#define NSPR 24
static void spr(u16 i, s16 x, s16 y, u8 size, u16 attr){ VDP_setSpriteFull(i, x, y, size, attr, i<NSPR-1 ? i+1 : 0); }
static void sprite(u16 i, s16 x, s16 y, u16 attr){ spr(i, x, y, SPRITE_SIZE(3,3), attr); }
static void hide_sprite(u16 i){ spr(i, -40, -40, SPRITE_SIZE(1,1), 0); }
static void hide_all_sprites(void){ for(int i=0;i<NSPR;i++) hide_sprite(i); }
static void sprites_flush(void){ VDP_updateSprites(NSPR, DMA_QUEUE); }
static void place_cursor(void){
    sprite(0, (BX+cur_c*3)*8, (BY+cur_r*3)*8, TILE_ATTR_FULL(PAL3,1,0,0,t_cursor));
    if(sel>=0){ int dr,dc; sq_disp(sel,&dr,&dc); sprite(1,(BX+dc*3)*8,(BY+dr*3)*8,TILE_ATTR_FULL(PAL3,1,0,0,t_cursor+9)); }
    else hide_sprite(1);
}
/* ---- particles (sprites 4..15) ---- */
#define NPART 12
typedef struct { s16 x,y,vx,vy; u8 life; u8 tile; } Part;
static Part parts[NPART];
static void parts_burst(s16 cx, s16 cy, u8 frog){
    for(int i=0;i<NPART;i++){
        Part *p=&parts[i];
        u16 r=random();
        s16 a=(i*85 + (r&31))&1023;
        s16 sp=96+(r>>8 & 63);
        p->x=cx<<4; p->y=cy<<4;
        p->vx=(sinFix16(a+256)*sp)>>6; p->vy=((sinFix16(a)*sp)>>6) - 40;
        p->life=22+(r&7); p->tile= frog ? 4 : 3;
    }
}
static void parts_update(void){
    for(int i=0;i<NPART;i++){
        Part *p=&parts[i];
        if(!p->life){ hide_sprite(4+i); continue; }
        p->life--; p->x+=p->vx>>2; p->y+=p->vy>>2; p->vy+=5;
        spr(4+i, (p->x>>4)-4, (p->y>>4)-4, SPRITE_SIZE(1,1), TILE_ATTR_FULL(PAL3,1,0,0,t_misc+p->tile));
    }
}
static u8 parts_active(void){ for(int i=0;i<NPART;i++) if(parts[i].life) return 1; return 0; }

/* ---------------- panel ---------------- */
static const char *side_name[2]={"МЫШИ","ЛЯГУШКИ"};
static void panel_msg(const char *a, const char *b){
    clear_a(PX,20,12,3);
    if(a) text_c(a,PX,12,20,PAL3);
    if(b) text_c(b,PX,12,21,PAL3);
}
static void panel_counts(void){
    char b[16];
    for(int s=0;s<2;s++){
        u16 y = s ? 11 : 7;
        clear_a(PX,y,12,3);
        text_c(side_name[s],PX,12,y,PAL3);
        strcpy(b,"Воины "); num(b+strlen(b), popcount32(pos.men[s])); text(b,PX+1,y+1,PAL2);
        strcpy(b,"Цари  "); num(b+strlen(b), popcount32(pos.kings[s])); text(b,PX+1,y+2,PAL2);
    }
    /* side to move */
    clear_a(PX,1,12,5);
    text_c("ХОД",PX,12,1,PAL3);
    int k = pos.side==SIDE_MICE ? PK_MOUSE : PK_FROG;
    for(int tx=0;tx<3;tx++) for(int ty=0;ty<3;ty++)
        VDP_setTileMapXY(BG_A, TILE_ATTR_FULL(kpal(k),1,0,0,t_pieces+k*9+tx*3+ty), PX+1+tx, 2+ty);
    text(side_name[pos.side], PX+4, 3, PAL2);
}
static void panel_static(void){
    clear_a(PX,15,12,4);
    text_c("Правила:",PX,12,15,PAL3);
    text_c(opt_rules==RULES_RUSSIAN ? "Русские" : "Английские",PX,12,16,PAL2);
    static const char *lv[3]={"Легко","Средне","Трудно"};
    text_c(opt_players==2 ? "2 игрока" : lv[opt_level],PX,12,18,PAL2);
    text_c("START-меню",PX,12,25,PAL3);
}

/* ---------------- move helpers ---------------- */
static void refresh_legal(void){ nlegal=gen_moves(&pos,opt_rules,legal); }
static int is_human_turn(void){ return opt_players==2 || pos.side==opt_side; }
static int has_captures(void){ return nlegal>0 && legal[0].ncapt>0; }
static int candidates_match(const Move *m){
    if(m->from!=sel || m->n<nprefix) return 0;
    for(int i=0;i<nprefix;i++) if(m->path[i]!=prefix[i]) return 0;
    return 1;
}
static void draw_markers(int on){
    if(sel<0) return;
    for(int i=0;i<nlegal;i++){
        const Move *m=&legal[i];
        if(!candidates_match(m) || m->n<=nprefix) continue;
        int t=m->path[nprefix]; if(piece_at(&pos,t)>=0) continue;
        int dr,dc; sq_disp(t,&dr,&dc); put_glyph9(dr,dc,on?PK_MARK:-1);
    }
    for(int i=0;i<nprefix;i++){ int dr,dc; sq_disp(prefix[i],&dr,&dc); if(piece_at(&pos,prefix[i])<0) put_glyph9(dr,dc,on?PK_MARK:-1); }
}
static void sq_px(int sq,s16 *x,s16 *y){ int dr,dc; sq_disp(sq,&dr,&dc); *x=(BX+dc*3)*8; *y=(BY+dr*3)*8; }

static void shake(u16 n){
    for(u16 f=0;f<n;f++){
        s16 o = (f&1) ? -(s16)((n-f)>>1) : (s16)((n-f)>>1);
        VDP_setVerticalScroll(BG_A,o); VDP_setVerticalScroll(BG_B,o);
        parts_update(); sprites_flush(); frame();
    }
    VDP_setVerticalScroll(BG_A,0); VDP_setVerticalScroll(BG_B,0);
}
static void animate_move(const Move *m){
    int side=pos.side;
    int k=piece_at(&pos,m->from);
    int dr,dc; sq_disp(m->from,&dr,&dc); put_glyph9(dr,dc,-1);
    s16 x0,y0; sq_px(m->from,&x0,&y0);
    int prev=m->from;
    hide_sprite(0); hide_sprite(1);
    for(int i=0;i<m->n;i++){
        int to=m->path[i]; s16 x1,y1; sq_px(to,&x1,&y1);
        int dist=abs(sq_row(to)-sq_row(prev));
        int steps=10+dist*4;
        for(int f=1;f<=steps;f++){
            /* smoothstep easing + parabolic hop; the ground shadow stays on the board */
            s32 t=(f<<8)/steps; s32 e=(t*t*(768-2*t))>>16;
            s16 x=x0+(s16)(((s32)(x1-x0)*e)>>8), y=y0+(s16)(((s32)(y1-y0)*e)>>8);
            s16 hmax = m->ncapt ? 16 : 5 + dist;
            s16 hop = (s16)((s32)hmax*4*t*(256-t)>>16);
            sprite(2,x,y-hop,TILE_ATTR_FULL(kpal(k),1,0,0,t_pieces+k*9));
            if(VOL) spr(3,x,y+17,SPRITE_SIZE(3,1),TILE_ATTR_FULL(PAL3,1,0,0,t_misc));
            parts_update(); sprites_flush(); frame();
        }
        if(m->ncapt){
            int r0=sq_row(prev),c0=sq_col(prev),r1=sq_row(to),c1=sq_col(to);
            int sr=r1>r0?1:-1, sc=c1>c0?1:-1;
            for(int r=r0+sr,c=c0+sc; r!=r1; r+=sr,c+=sc){
                int s=rc_to_sq(r,c);
                if(s>=0 && (m->capt&BIT(s))){
                    int victim=piece_at(&pos,s);
                    u8 frog = victim==PK_FROG||victim==PK_FROG_K;
                    if(frog) SFX(sfx_splash); else SFX(sfx_capture);
                    int vr,vc; sq_disp(s,&vr,&vc);
                    /* duel: the victim flashes, the board shakes, sparks fly */
                    for(int fl=0;fl<3;fl++){ put_glyph9_pal(vr,vc,victim,PAL3); wait_frames(2); put_glyph9(vr,vc,victim); wait_frames(2); }
                    parts_burst((BX+vc*3)*8+12,(BY+vr*3)*8+12,frog);
                    shake(8);
                    for(int ph=0;ph<3;ph++){ put_glyph9(vr,vc,PK_BURST0+ph); for(int w=0;w<4;w++){ parts_update(); sprites_flush(); frame(); } }
                    if(opt_rules==RULES_ENGLISH) put_glyph9(vr,vc,-1);  /* removed at once */
                }
            }
        } else SFX(sfx_move);
        int prow = side==SIDE_MICE ? 0 : 7;
        if((k==PK_MOUSE||k==PK_FROG) && sq_row(to)==prow){ k++; SFX(sfx_promote); }
        prev=to; x0=x1; y0=y1;
    }
    hide_sprite(2); hide_sprite(3);
    while(parts_active()){ parts_update(); sprites_flush(); frame(); }
    sprites_flush();
}

static void do_move(const Move *m){
    if(nhist<HIST){ hist[nhist]=pos; hist_from[nhist]=last_from; hist_to[nhist]=last_to; nhist++; }
    draw_markers(0); sel=-1; nprefix=0; place_cursor(); panel_msg(0,0);
    animate_move(m);
    make_move(&pos,m,opt_rules);
    last_from=m->from; last_to=m->to;
    draw_board(); draw_pieces(); panel_counts(); panel_msg(0,0);
    refresh_legal();
}
static u8 draw_reason;   /* 0 = 30 moves without progress, 1 = threefold repetition */
static void check_end(void){
    result=-1;
    if(nlegal==0) result = pos.side^1;
    else if(pos.quiet>=60){ result=2; draw_reason=0; }
    else if(is_threefold(hist,nhist,&pos)){ result=2; draw_reason=1; }
}

/* ---------------- AI hooks ---------------- */
/* the budget is counted in nodes inside ai.c (deterministic); the console only animates meanwhile */
void ai_time_start(int level){ (void)level; }
int ai_time_up(void){ return 0; }
unsigned ai_rand(void){ return random(); }

/* ---------------- pause menu ---------------- */
static int pause_menu(void){
    static const char *it[3]={"Продолжить","Отмена хода","В меню"};
    int s=0;
    clear_a(PX,19,12,7);
    for(;;){
        for(int i=0;i<3;i++){ clear_a(PX,20+i,12,1); text(i==s?">":" ",PX,20+i,PAL3); text(it[i],PX+1,20+i,i==s?PAL3:PAL2); }
        frame();
        if(pad_press&BUTTON_UP){ s=(s+2)%3; SFX(sfx_select);} 
        if(pad_press&BUTTON_DOWN){ s=(s+1)%3; SFX(sfx_select);} 
        if(pad_press&BUTTON_START){ s=0; break; }
        if(pad_press&(BUTTON_A|BUTTON_C)) break;
        if(pad_press&BUTTON_B){ s=0; break; }
    }
    clear_a(PX,19,12,7); panel_static(); panel_msg(0,0);
    return s;
}
static void undo(void){
    if(nhist==0){ SFX(sfx_error); return; }
    do{
        nhist--; pos=hist[nhist]; last_from=hist_from[nhist]; last_to=hist_to[nhist];
    } while(opt_players==1 && pos.side!=opt_side && nhist>0);
    sel=-1; nprefix=0; refresh_legal();
    draw_board(); draw_pieces(); panel_counts();
}

/* ---------------- v2 effects: H-int sky, VInt animation ---------------- */
static vu8 hint_on;
static const u16 *hint_tab;          /* SKY_BANDS pairs of colours for PAL0 idx 1/2 */
HINTERRUPT_CALLBACK hint_sky(void){
    u16 v = (*(vu16*)0xC00008) >> 8;
    u16 b = (v+1)>>2; if(b>=SKY_BANDS) b=SKY_BANDS-1;
    const u32 c = *(const u32*)(hint_tab + b*2);
    *(vu32*)VDP_CTRL_PORT = VDP_WRITE_CRAM_ADDR(2);
    *(vu32*)VDP_DATA_PORT = c;
}
static void hint_start(const u16 *tab){
    hint_tab=tab;
    SYS_disableInts();
    SYS_setHIntCallback(hint_sky);
    VDP_setHIntCounter(3);          /* every 4 lines */
    VDP_setHInterrupt(TRUE);
    hint_on=1;
    SYS_enableInts();
}
static void hint_stop(void){
    if(!hint_on) return;
    SYS_disableInts(); VDP_setHInterrupt(FALSE); hint_on=0; SYS_enableInts();
}

/* idle "breathing" of the pieces: swap the 36 man/king tiles */
static u8 idle_on, idle_ph;
static void idle_swap(u16 mode){
    idle_ph^=1;
    VDP_loadTileData(idle_ph ? tiles2_pieces_b : tiles2_pieces, t_pieces, TILES2_PIECES_B_N, mode);
}
static void anim_tick(void){
    anim_t++;
    if(idle_on && VOL && (anim_t%24)==0) idle_swap(DMA_QUEUE);
}
/* runs inside the vertical interrupt while the AI searches: the screen keeps living */
static void vint_ai(void){
    if(hint_on){ *(vu32*)VDP_CTRL_PORT = VDP_WRITE_CRAM_ADDR(2); *(vu32*)VDP_DATA_PORT = *(const u32*)hint_tab; }
    if(!ai_busy) return;
    anim_t++;
    if(idle_on && VOL && (anim_t%24)==0) idle_swap(DMA);
    if((anim_t&15)==0){
        u16 n=(anim_t>>4)&3;
        for(u16 i=0;i<3;i++)
            VDP_setTileMapXY(BG_A, i<n ? TILE_ATTR_FULL(PAL3,1,0,0,T_FONT+glyph('.')) : 0, PX+8+i, 20);
    }
    /* cursor pulse continues too */
    static const u16 pulse[8]={0x0EE,0x0CE,0x0AE,0x08C,0x08C,0x0AE,0x0CE,0x0EE};
    *(vu32*)VDP_CTRL_PORT = VDP_WRITE_CRAM_ADDR((48+2)*2);
    *(vu16*)VDP_DATA_PORT = pulse[(anim_t>>3)&7];
}

/* ---------------- screens ---------------- */
static void set_pals(const u16 *p0,const u16 *p1,const u16 *p2,const u16 *p3){
    memcpy(pal_all,p0,32); memcpy(pal_all+16,p1,32); memcpy(pal_all+32,p2,32); memcpy(pal_all+48,p3,32);
}
static void put_map(VDPPlane pl,const u16 *m,u16 w,u16 h,u16 x0,u16 y0,u16 base,u16 pal,u16 prio){
    for(u16 y=0;y<h;y++) for(u16 x=0;x<w;x++) VDP_setTileMapXY(pl,TILE_ATTR_FULL(pal,prio,0,0,base+m[y*w+x]),x0+x,y0+y);
}
static void big_sprite(u16 i,s16 x,s16 y,u16 who,u16 pal){
    /* 64x64 = four 32x32 sprites */
    u16 b=TT_BIG+who*64;
    spr(i,  x,   y,   SPRITE_SIZE(4,4),TILE_ATTR_FULL(pal,1,0,0,b));
    spr(i+1,x+32,y,   SPRITE_SIZE(4,4),TILE_ATTR_FULL(pal,1,0,0,b+16));
    spr(i+2,x,   y+32,SPRITE_SIZE(4,4),TILE_ATTR_FULL(pal,1,0,0,b+32));
    spr(i+3,x+32,y+32,SPRITE_SIZE(4,4),TILE_ATTR_FULL(pal,1,0,0,b+48));
}
static void load_title_tiles(void){
    VDP_loadTileData(tiles2_land,TT_LAND,TILES2_LAND_N,DMA);
    VDP_loadTileData(tiles2_clouds,TT_CLOUDS,TILES2_CLOUDS_N,DMA);
    VDP_loadTileData(tiles2_logo,TT_LOGO,TILES2_LOGO_N,DMA);
    VDP_loadTileData(tiles2_big,TT_BIG,TILES2_BIG_N,DMA);
    VDP_loadTileData(tiles2_misc,TT_MISC,TILES2_MISC_N,DMA);
    static const u32 solid[8]={0x11111111,0x11111111,0x11111111,0x11111111,0x11111111,0x11111111,0x11111111,0x11111111};
    static const u32 hl[8]={0xEEEEEEEE,0xEEEEEEEE,0xEEEEEEEE,0xEEEEEEEE,0xEEEEEEEE,0xEEEEEEEE,0xEEEEEEEE,0xEEEEEEEE};
    VDP_loadTileData(solid,TT_SOLID,1,DMA);
    for(u16 i=0;i<4;i++) VDP_loadTileData(hl,TT_HL+i,1,DMA);
}

static const char homer[] =
  "   ~ Начиная, молю я хор Геликонских Муз сойти в моё сердце ради песни, что недавно я записал на табличках, — "
  "о безмерной битве, воинственном деле Арея, о том, как мыши пошли на лягушек, подобные земнородным Гигантам ~ "
  "Так говорят средь людей; и было такое начало ~ ";
static s16 hs_a[224];

static int screen_title(void){
    fade_out(); reset_screen();
    VDP_setEnable(FALSE);
    load_title_tiles();
    put_map(BG_B,map2_land,40,28,0,0,TT_LAND,PAL0,1);
    put_map(BG_A,map2_logo,40,7,0,0,TT_LOGO,PAL3,1);
    for(u16 rep=0;rep<2;rep++) put_map(BG_A,map2_clouds,32,5,rep*32,9,TT_CLOUDS,PAL0,1);
    /* dark band behind the Homeric scroller (plane B, rows 24-26) */
    VDP_fillTileMapRect(BG_B,TILE_ATTR_FULL(PAL3,1,0,0,TT_SOLID),0,24,64,3);
    text_prio=1;
    VDP_setHilightShadow(TRUE);
    VDP_setScrollingMode(HSCROLL_LINE,VSCROLL_PLANE);
    VDP_setEnable(TRUE);
    set_pals(pal2_land,pal2_mice,pal2_frogs,pal2_logo);
    music(0);
    hint_start(sky_grad);
    fade_in();
    hint_start(sky_grad);
    static s16 hs_b[224];
    u16 t=0; s16 sc=0; u16 col=0; const char *hp=homer; u16 subpx=0;
    s16 gx=-40;
    for(;;){
        /* parallax: clouds drift, lower cloud lines faster; scroller band */
        for(u16 y=0;y<224;y++){
            s16 a=0, b=0;
            if(y>=72 && y<112) a = -(s16)((t*(1+((y-72)>>3)))>>3);
            else if(y>=192 && y<216) a = -sc;
            if((y>=140 && y<160) || (y>=175)) b = (sinFix16((y*24+t*6)&1023)>>5);   /* water ripple ±1..2 px */
            hs_a[y]=a; hs_b[y]=b;
        }
        VDP_setHorizontalScrollLine(BG_A,0,hs_a,224,DMA_QUEUE);
        VDP_setHorizontalScrollLine(BG_B,0,hs_b,224,DMA_QUEUE);
        /* stream the scroller one glyph per 8 px */
        subpx++; sc++;
        if((subpx&7)==1){
            u32 cp=(u8)*hp; int len=1;
            if(cp>=0xC0){ if(cp<0xE0){ cp=((cp&0x1F)<<6)|(hp[1]&0x3F); len=2; } else { cp=((cp&0x0F)<<12)|((hp[1]&0x3F)<<6)|(hp[2]&0x3F); len=3; } }
            hp+=len; if(!*hp) hp=homer;
            u16 x=((sc>>3)+41)&63;
            SYS_disableInts();
            VDP_setTileMapXY(BG_A, TILE_ATTR_FULL(PAL3,1,0,0,T_FONT+glyph(cp)), x, 25);
            SYS_enableInts();
            col++;
        }
        /* blinking prompt */
        SYS_disableInts();
        if((t&63)==0) text_c("НАЖМИ START",0,40,21,PAL3);
        if((t&63)==44) clear_a(0,21,40,1);
        SYS_enableInts();
        /* bobbing kings + highlight glint over the logo */
        s16 bob=(sinFix16((t*8)&1023)>>4);
        big_sprite(0,8,120+bob,0,PAL1);
        big_sprite(4,248,120-bob,1,PAL2);
        gx+=3; if(gx>420) gx=-60;
        spr(8,gx,0,SPRITE_SIZE(2,4),TILE_ATTR_FULL(PAL3,1,0,0,TT_MISC+5));
        spr(9,gx,32,SPRITE_SIZE(2,3),TILE_ATTR_FULL(PAL3,1,0,0,TT_MISC+13));
        for(u16 i=10;i<NSPR;i++) hide_sprite(i);
        sprites_flush();
        frame(); t++;
        if(pad_press&(BUTTON_START|BUTTON_A|BUTTON_C)) break;
    }
    SFX(sfx_select);
    return ST_MENU;
}

static int screen_menu(void){
    fade_out(); reset_screen();
    VDP_setEnable(FALSE);
    load_title_tiles();
    put_map(BG_B,map2_land,40,28,0,0,TT_LAND,PAL0,0);     /* low priority → shadowed backdrop */
    put_map(BG_A,map2_logo,40,7,0,0,TT_LOGO,PAL3,1);
    text_prio=0;
    VDP_setHilightShadow(TRUE);
    VDP_setEnable(TRUE);
    set_pals(pal2_land,pal2_mice,pal2_frogs,pal2_logo);
    hint_start(sky_grad);
    fade_in();
    hint_start(sky_grad);
    int s=0; const int N=6; u16 t=0;
    s16 bar_y=-1;
    for(;;){
        static const char *lab[6]={"Игроки","Сторона","Сложность","Правила","Графика","НАЧАТЬ БИТВУ"};
        static const char *lv[3]={"Легко","Средне","Трудно"};
        SYS_disableInts();
        for(int i=0;i<N;i++){
            u16 y=9+i*2+(i==5?1:0); clear_a(9,y,23,1);
            if(i==5){ text_c(lab[i],9,23,y,PAL3); continue; }
            text(lab[i],10,y,i==s?PAL3:PAL2);
            const char *v="";
            if(i==0) v= opt_players==1 ? "1 игрок" : "2 игрока";
            if(i==1) v= opt_players==2 ? "—" : (opt_side==SIDE_MICE ? "Мыши" : "Лягушки");
            if(i==2) v= opt_players==2 ? "—" : lv[opt_level];
            if(i==3) v= opt_rules==RULES_RUSSIAN ? "Русские" : "Английские";
            if(i==4) v= opt_gfx==0 ? "Объёмная" : "Простая";
            text(v,21,y,i==s?PAL3:PAL2);
        }
        clear_a(1,26,38,1);
        text_c("↑↓ выбор   ←→ значение   A начать",0,40,26,PAL2);
        SYS_enableInts();
        /* highlight bar: sprites in the highlight colour lift the shadowed row */
        s16 ty=(9+s*2+(s==5?1:0))*8;
        if(bar_y<0) bar_y=ty; else bar_y+= (ty-bar_y+ (ty>bar_y?3:-3))/4;
        for(u16 i=0;i<6;i++) spr(i,72+i*32,bar_y,SPRITE_SIZE(4,1),TILE_ATTR_FULL(PAL3,1,0,0,TT_HL));
        s16 bob=(sinFix16((t*8)&1023)>>5);
        u16 m = opt_side==SIDE_MICE || opt_players==2;
        /* warriors (big sprites 2 and 3) */
        spr(6,  4,  88+bob, SPRITE_SIZE(4,4),TILE_ATTR_FULL(PAL1,1,0,0,TT_BIG+128));
        spr(7,  36, 88+bob, SPRITE_SIZE(4,4),TILE_ATTR_FULL(PAL1,1,0,0,TT_BIG+128+16));
        spr(8,  4,  120+bob,SPRITE_SIZE(4,4),TILE_ATTR_FULL(PAL1,1,0,0,TT_BIG+128+32));
        spr(9,  36, 120+bob,SPRITE_SIZE(4,4),TILE_ATTR_FULL(PAL1,1,0,0,TT_BIG+128+48));
        spr(10, 252,88-bob, SPRITE_SIZE(4,4),TILE_ATTR_FULL(PAL2,1,0,0,TT_BIG+192));
        spr(11, 284,88-bob, SPRITE_SIZE(4,4),TILE_ATTR_FULL(PAL2,1,0,0,TT_BIG+192+16));
        spr(12, 252,120-bob,SPRITE_SIZE(4,4),TILE_ATTR_FULL(PAL2,1,0,0,TT_BIG+192+32));
        spr(13, 284,120-bob,SPRITE_SIZE(4,4),TILE_ATTR_FULL(PAL2,1,0,0,TT_BIG+192+48));
        (void)m;
        for(u16 i=14;i<NSPR;i++) hide_sprite(i);
        sprites_flush();
        frame(); t++;
        int d=0;
        if(pad_press&BUTTON_UP){ s=(s+N-1)%N; SFX(sfx_select);}
        if(pad_press&BUTTON_DOWN){ s=(s+1)%N; SFX(sfx_select);}
        if(pad_press&BUTTON_LEFT) d=-1;
        if(pad_press&BUTTON_RIGHT) d=1;
        if((pad_press&(BUTTON_A|BUTTON_C)) && s!=5) d=1;
        if(d){
            if(s==0) opt_players = opt_players==1 ? 2 : 1;
            if(s==1 && opt_players==1) opt_side^=1;
            if(s==2 && opt_players==1) opt_level=(opt_level+3+d)%3;
            if(s==3) opt_rules^=1;
            if(s==4) opt_gfx^=1;
            if(s!=5) SFX(sfx_move);
        }
        if((pad_press&BUTTON_START) || ((pad_press&(BUTTON_A|BUTTON_C)) && s==5)){ SFX(sfx_promote); break; }
        if(pad_press&BUTTON_B) return ST_TITLE;
    }
    return intro_seen ? ST_GAME : ST_INTRO;
}

static const char *const intro_pages[5][7]={
 {"ПЕСНЬ ПЕРВАЯ","Внемлите, смертные! Поведаю я","о великой войне мышей и лягушек,","что разгорелась некогда у тихого","пруда на радость и забаву","олимпийским богам.",0},
 {"ПЕСНЬ ВТОРАЯ","Мышонок Психарпакс, сын царя","Троксарта, бежав от хорька, припал","к воде напиться. Его заметил","Физигнат, царь лягушек, и позвал","в гости: «Садись мне на спину!»",0},
 {"ПЕСНЬ ТРЕТЬЯ","Но посреди пруда поднялся из волн","водяной змей. Испуганный Физигнат","нырнул на дно, забыв о госте, —","и Психарпакс утонул, проклиная","коварного царя лягушек.",0},
 {"ПЕСНЬ ЧЕТВЁРТАЯ","Мыши поклялись отомстить. Они","надели скорлупки орехов вместо","шлемов и взяли иглы вместо копий.","Лягушки укрылись листьями капусты","и вооружились острым тростником.",0},
 {"ПЕСНЬ ПЯТАЯ","Зевс созвал богов взглянуть на","битву с высот Олимпа. Ныне судьба","войны — в твоих руках, стратег!","Двенадцать воинов с каждой","стороны. Бить — обязательно!",0},
};
static const Image *const intro_imgs[5]={&intro1_img,&intro2_img,&intro3_img,&intro4_img,&intro5_img};
static int screen_intro(void){
    for(int p=0;p<5;p++){
        fade_out(); reset_screen();
        VDP_drawImageEx(BG_B, intro_imgs[p], TILE_ATTR_FULL(PAL0,0,0,0,T_DYN), 4,0, FALSE, TRUE);
        set_pals(intro_imgs[p]->palette->data,pal2_mice,pal2_frogs,pal2_gold);
        text_c(intro_pages[p][0],0,40,19,PAL3);
        text_c("A — далее    START — пропустить",0,40,27,PAL2);
        fade_in();
        int skip=0;
        for(int l=1;intro_pages[p][l];l++){
            const char *s=intro_pages[p][l];
            u16 n=utf8_len(s); u16 x0=2+(36-n)/2;
            char buf[80]; const char *q=s; int bi=0;
            while(*q){
                do { buf[bi++]=*q++; } while(*q && ((*q&0xC0)==0x80));
                buf[bi]=0;
                if(!skip){ text(buf,x0,20+l,PAL2); frame(); frame(); if(pad_press&(BUTTON_A|BUTTON_C)) skip=1; if(pad_press&BUTTON_START){ intro_seen=1; return ST_GAME; } }
            }
            text(s,x0,20+l,PAL2);
        }
        for(;;){ frame(); if(pad_press&(BUTTON_A|BUTTON_C)) break; if(pad_press&BUTTON_START){ intro_seen=1; return ST_GAME; } }
        SFX(sfx_select);
    }
    intro_seen=1;
    return ST_GAME;
}

static int screen_game(void){
    fade_out(); reset_screen();
    if(VOL){ load_game_tiles2(); set_pals(pal2_board,pal2_mice,pal2_frogs,pal2_gold); VDP_setHilightShadow(TRUE); }
    else { load_game_tiles(); build_pal(pal_board); }
    idle_ph=0; idle_on=1; memset(parts,0,sizeof(parts)); hide_all_sprites();
    flip = (opt_players==1 && opt_side==SIDE_FROGS);
    pos_start(&pos); nhist=0; last_from=last_to=-1; sel=-1; nprefix=0; cur_r=5; cur_c=0;
    refresh_legal(); result=-1;
    draw_frame_static(); draw_board(); draw_pieces(); panel_counts(); panel_static();
    place_cursor(); sprites_flush();
    music(1);
    fade_in();
    u16 t=0;
    while(result<0){
        if(!is_human_turn()){
            hide_sprite(0); hide_sprite(1); sprites_flush();
            panel_msg("Думаю",0); frame(); frame();
            /* the search runs in the foreground; the vertical interrupt keeps animating */
            u16 t0=anim_t;
            ai_busy=1;
            int i = ai_choose(&pos,opt_rules,opt_level,legal,nlegal);
            ai_busy=0;
            while((u16)(anim_t-t0)<20) frame();     /* minimum visible pause */
            panel_msg(0,0);
            do_move(&legal[i]); check_end();
            if(result<0 && has_captures() && is_human_turn()) panel_msg("Взятие","обязательно!");
            continue;
        }
        place_cursor(); sprites_flush();
        /* gentle pulse of the cursor colour */
        static const u16 pulse[8]={0x0EE,0x0CE,0x0AE,0x08C,0x08C,0x0AE,0x0CE,0x0EE};
        PAL_setColor(48+2, pulse[(t>>3)&7]);
        frame(); t++;
        if(pad_press&BUTTON_UP && cur_r>0) cur_r--;
        if(pad_press&BUTTON_DOWN && cur_r<7) cur_r++;
        if(pad_press&BUTTON_LEFT && cur_c>0) cur_c--;
        if(pad_press&BUTTON_RIGHT && cur_c<7) cur_c++;
        if(pad_press&BUTTON_B){ draw_markers(0); sel=-1; nprefix=0; }
        if(pad_press&BUTTON_START){
            int r=pause_menu();
            if(r==1){ draw_markers(0); undo(); }
            if(r==2){ idle_on=0; return ST_MENU; }
            continue;
        }
        if(pad_press&(BUTTON_A|BUTTON_C)){
            int s=disp_sq(cur_r,cur_c);
            if(s<0){ SFX(sfx_error); continue; }
            int own = piece_at(&pos,s)>=0 && ((pos.men[pos.side]|pos.kings[pos.side])&BIT(s));
            if(own && nprefix==0){
                int any=0; for(int i=0;i<nlegal;i++) if(legal[i].from==s) any=1;
                if(sel==s){ draw_markers(0); sel=-1; continue; }
                if(any){ draw_markers(0); sel=s; nprefix=0; draw_markers(1); SFX(sfx_select); panel_msg(0,0); }
                else { SFX(sfx_error); if(has_captures()) panel_msg("Взятие","обязательно!"); else panel_msg("Этой шашке","некуда идти"); }
                continue;
            }
            if(sel<0){ SFX(sfx_error); continue; }
            /* is s a next landing square? */
            int found=-1, complete=-1, longer=0;
            for(int i=0;i<nlegal;i++){
                const Move *m=&legal[i];
                if(!candidates_match(m) || m->n<=nprefix || m->path[nprefix]!=s) continue;
                found=i; if(m->n==nprefix+1) complete=i; else longer=1;
            }
            if(found<0){ SFX(sfx_error); if(has_captures()) panel_msg("Взятие","обязательно!"); continue; }
            draw_markers(0);
            if(complete>=0 && !longer){ do_move(&legal[complete]); check_end(); continue; }
            prefix[nprefix++]=s; draw_markers(1); SFX(sfx_select);
            panel_msg("Бей дальше!",0);
        }
    }
    wait_frames(50);
    idle_on=0;
    return ST_END;
}

static int screen_end(void){
    fade_out(); reset_screen();
    const char *head, *l1, *l2;
    int human_won = opt_players==1 && result==opt_side;
    const Image *img = result==2 ? &end_draw_img : result==SIDE_MICE ? &end_mice_img : &end_frogs_img;
    VDP_drawImageEx(BG_B, img, TILE_ATTR_FULL(PAL0,0,0,0,T_DYN), 4,0, FALSE, TRUE);
    set_pals(img->palette->data,pal2_mice,pal2_frogs,pal2_gold);
    if(result==2){
        head="НИЧЬЯ";
        if(draw_reason){ l1="Трижды повторилась позиция —"; l2="боги объявили перемирие."; }
        else { l1="Зевс послал на поле раков —"; l2="и война окончилась за один день."; }
        music(3);
    } else {
        if(opt_players==2) head = result==SIDE_MICE ? "ПОБЕДИЛИ МЫШИ!" : "ПОБЕДИЛИ ЛЯГУШКИ!";
        else head = human_won ? "ПОБЕДА!" : "ПОРАЖЕНИЕ";
        if(result==SIDE_MICE){ l1="Психарпакс отомщён!"; l2="Мыши пируют на берегу пруда."; }
        else { l1="Физигнат удержал свой пруд!"; l2="Лягушки славят царя кваканьем."; }
        music(opt_players==1 && !human_won ? 3 : 2);
    }
    text_c(head,0,40,17,PAL3);
    text_c(l1,0,40,19,PAL2); text_c(l2,0,40,21,PAL2);
    {
        char b[40]; strcpy(b,"Ходов сделано: "); num(b+strlen(b),(nhist+1)/2); text_c(b,0,40,23,PAL2);
    }
    text_c("A — реванш    START — меню",0,40,26,PAL3);
    fade_in();
    for(;;){
        frame();
        if(pad_press&(BUTTON_A|BUTTON_C)) return ST_GAME;
        if(pad_press&BUTTON_START) return ST_MENU;
    }
}

int main(bool hard){
    VDP_setScreenWidth320();
    VDP_setPlaneSize(64,32,TRUE);
    VDP_setBackgroundColor(0);
    PAL_setColors(0, palette_black, 64, CPU);
    VDP_loadTileData(tiles_font, T_FONT, TILES_FONT_N, DMA);
    rules_init();
    SYS_setVIntCallback(vint_ai);
    XGM2_load(bgm);
    setRandomSeed(0x1F2E);
    int st=ST_TITLE, prev=-1;
    for(;;){
        int nx=st;
        if(st==ST_TITLE) nx=screen_title();
        else if(st==ST_MENU){ if(prev!=ST_TITLE){ cur_track=-1; music(0);} nx=screen_menu(); }
        else if(st==ST_INTRO) nx=screen_intro();
        else if(st==ST_GAME){ setRandomSeed(vtimer^0x5A5A); nx=screen_game(); }
        else nx=screen_end();
        prev=st; st=nx;
    }
    return 0;
}
