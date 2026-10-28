/* БАТРАХОМИОМАХИЯ — Война мышей и лягушек. Шашки для Sega Mega Drive / Genesis (SGDK). */
#include <genesis.h>
#include "rules.h"
#include "ai.h"
#include "gfx_data.h"
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

static Pos pos;
static Move legal[MAX_MOVES];
static int nlegal;
static u8 flip;
#define HIST 160
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
static void frame(void){ SYS_doVBlankProcess(); input_update(); }
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
        VDP_setTileMapXY(BG_A, TILE_ATTR_FULL(pal,1,0,0,T_FONT+glyph(cp)), x++, y);
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
static void fade_out(void){ PAL_fadeOutAll(16, FALSE); }
static void fade_in(void){ PAL_fadeInAll(pal_all, 16, FALSE); }

static void reset_screen(void){
    VDP_setEnable(FALSE);
    VDP_clearPlane(BG_A, TRUE); VDP_clearPlane(BG_B, TRUE);
    VDP_resetSprites(); VDP_updateSprites(1, DMA);
    VDP_setEnable(TRUE);
}
static void load_game_tiles(void){
    VDP_loadTileData(tiles_board, T_BOARD, TILES_BOARD_N, DMA);
    VDP_loadTileData(tiles_frame, T_FRAME, TILES_FRAME_N, DMA);
    VDP_loadTileData(tiles_column, T_COLUMN, TILES_COLUMN_N, DMA);
    VDP_loadTileData(tiles_pieces, T_PIECES, TILES_PIECES_N, DMA);
    VDP_loadTileData(tiles_cursor, T_CURSOR, TILES_CURSOR_N, DMA);
    VDP_loadTileData(tiles_big, T_BIG, TILES_BIG_N, DMA);
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
    int dark=(dr+dc)&1; u16 base=T_BOARD + (dark ? (hl?18:9) : 0);
    for(int ty=0;ty<3;ty++) for(int tx=0;tx<3;tx++)
        VDP_setTileMapXY(BG_B, TILE_ATTR_FULL(PAL0,0,0,0,base+ty*3+tx), BX+dc*3+tx, BY+dr*3+ty);
}
static void put_glyph9(int dr,int dc,int k){
    for(int tx=0;tx<3;tx++) for(int ty=0;ty<3;ty++)
        VDP_setTileMapXY(BG_A, k<0 ? 0 : TILE_ATTR_FULL(PAL1,0,0,0,T_PIECES+k*9+tx*3+ty), BX+dc*3+tx, BY+dr*3+ty);
}
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
    friezes();
    for(u16 y=0;y<26;y++) for(u16 x=0;x<2;x++)
        VDP_setTileMapXY(BG_B, TILE_ATTR_FULL(PAL0,0,0,0,T_COLUMN+y*2+x), x, 1+y);
    VDP_fillTileMapRect(BG_B, MH, 3,1,24,1); VDP_fillTileMapRect(BG_B, MH, 3,26,24,1);
    VDP_fillTileMapRect(BG_B, MV, 2,2,1,24); VDP_fillTileMapRect(BG_B, MV, 27,2,1,24);
    VDP_setTileMapXY(BG_B,MC,2,1); VDP_setTileMapXY(BG_B,MC,27,1); VDP_setTileMapXY(BG_B,MC,2,26); VDP_setTileMapXY(BG_B,MC,27,26);
}

/* ---------------- sprites ---------------- */
static void sprite(u16 i, s16 x, s16 y, u16 attr){ VDP_setSpriteFull(i, x, y, SPRITE_SIZE(3,3), attr, i<2 ? i+1 : 0); }
static void hide_sprite(u16 i){ sprite(i, -40, -40, 0); }
static void sprites_flush(void){ VDP_updateSprites(3, DMA_QUEUE); }
static void place_cursor(void){
    sprite(0, (BX+cur_c*3)*8, (BY+cur_r*3)*8, TILE_ATTR_FULL(PAL3,1,0,0,T_CURSOR));
    if(sel>=0){ int dr,dc; sq_disp(sel,&dr,&dc); sprite(1,(BX+dc*3)*8,(BY+dr*3)*8,TILE_ATTR_FULL(PAL3,1,0,0,T_CURSOR+9)); }
    else hide_sprite(1);
}

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
        VDP_setTileMapXY(BG_A, TILE_ATTR_FULL(PAL1,0,0,0,T_PIECES+k*9+tx*3+ty), PX+1+tx, 2+ty);
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

static void animate_move(const Move *m){
    int side=pos.side;
    int k=piece_at(&pos,m->from);
    int dr,dc; sq_disp(m->from,&dr,&dc); put_glyph9(dr,dc,-1);
    s16 x0,y0; sq_px(m->from,&x0,&y0);
    int prev=m->from;
    for(int i=0;i<m->n;i++){
        int to=m->path[i]; s16 x1,y1; sq_px(to,&x1,&y1);
        int dist=abs(sq_row(to)-sq_row(prev));
        int steps=8+dist*3;
        for(int f=1;f<=steps;f++){
            /* ease-out with a little hop */
            s16 x=x0+(s16)((x1-x0)*f/steps), y=y0+(s16)((y1-y0)*f/steps);
            s16 hop = (m->ncapt) ? (s16)(f*(steps-f)*12/(steps*steps/4+1)/4) : 0;
            hide_sprite(0); hide_sprite(1);
            sprite(2,x,y-hop,TILE_ATTR_FULL(PAL1,1,0,0,T_PIECES+k*9));
            sprites_flush(); frame();
        }
        if(m->ncapt){
            /* the captured piece lies between prev and to on the diagonal */
            int r0=sq_row(prev),c0=sq_col(prev),r1=sq_row(to),c1=sq_col(to);
            int sr=r1>r0?1:-1, sc=c1>c0?1:-1;
            for(int r=r0+sr,c=c0+sc; r!=r1; r+=sr,c+=sc){
                int s=rc_to_sq(r,c);
                if(s>=0 && (m->capt&BIT(s))){
                    int victim=piece_at(&pos,s);
                    if(victim==PK_FROG||victim==PK_FROG_K) SFX(sfx_splash); else SFX(sfx_capture);
                    int vr,vc; sq_disp(s,&vr,&vc);
                    for(int ph=0;ph<3;ph++){ put_glyph9(vr,vc,PK_BURST0+ph); wait_frames(4); }
                    if(opt_rules==RULES_ENGLISH) put_glyph9(vr,vc,-1);  /* removed at once */
                }
            }
        } else SFX(sfx_move);
        /* promotion during the move (Russian: continues as a king) */
        int prow = side==SIDE_MICE ? 0 : 7;
        if((k==PK_MOUSE||k==PK_FROG) && sq_row(to)==prow){ k++; SFX(sfx_promote); }
        prev=to; x0=x1; y0=y1;
    }
    hide_sprite(2); sprites_flush();
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
static void check_end(void){
    result=-1;
    if(nlegal==0) result = pos.side^1;
    else if(pos.quiet>=60) result=2;
}

/* ---------------- AI hooks ---------------- */
static u32 ai_deadline;
void ai_time_start(int level){ static const u16 budget[3]={40,150,330}; ai_deadline=vtimer+budget[level]; }
int ai_time_up(void){ return vtimer>=ai_deadline; }
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

/* ---------------- screens ---------------- */
static int screen_title(void){
    fade_out(); reset_screen();
    VDP_drawImageEx(BG_B, &title_img, TILE_ATTR_FULL(PAL0,0,0,0,T_DYN), 0,0, FALSE, TRUE);
    build_pal(title_img.palette->data);
    music(0);
    fade_in();
    u16 t=0;
    for(;;){
        if((t&31)==0) text_c("НАЖМИ START",0,40,26,PAL3);
        if((t&31)==20) clear_a(0,26,40,1);
        frame(); t++;
        if(pad_press&(BUTTON_START|BUTTON_A|BUTTON_C)) break;
    }
    SFX(sfx_select);
    return ST_MENU;
}
static int screen_menu(void){
    fade_out(); reset_screen(); load_game_tiles(); build_pal(pal_board);
    friezes();
    VDP_fillTileMapRect(BG_B, MH, 0,4,40,1); VDP_fillTileMapRect(BG_B, MH, 0,23,40,1);
    text_c("ВОЙНА МЫШЕЙ И ЛЯГУШЕК",0,40,2,PAL3);
    big_emblem(0,1,10); big_emblem(1,31,10);
    text_c("Психарпакс",0,10,19,PAL2); text_c("Физигнат",30,10,19,PAL2);
    int s=0; const int N=5;
    fade_in();
    for(;;){
        static const char *lab[5]={"Игроки","Сторона","Сложность","Правила","НАЧАТЬ БИТВУ"};
        static const char *lv[3]={"Легко","Средне","Трудно"};
        for(int i=0;i<N;i++){
            u16 y=8+i*2+(i==4?1:0); clear_a(10,y,21,1);
            text(i==s?"»":" ",10,y,PAL3);
            if(i==4){ text_c(lab[i],10,21,y,i==s?PAL3:PAL2); continue; }
            text(lab[i],11,y,i==s?PAL3:PAL2);
            const char *v="";
            if(i==0) v= opt_players==1 ? "1 игрок" : "2 игрока";
            if(i==1) v= opt_players==2 ? "—" : (opt_side==SIDE_MICE ? "Мыши" : "Лягушки");
            if(i==2) v= opt_players==2 ? "—" : lv[opt_level];
            if(i==3) v= opt_rules==RULES_RUSSIAN ? "Русские" : "Английские";
            text(v,21,y,i==s?PAL3:PAL2);
        }
        clear_a(1,25,38,1);
        text_c("↑↓ выбор   ←→ значение   A начать",0,40,25,PAL2);
        frame();
        int d=0;
        if(pad_press&BUTTON_UP){ s=(s+N-1)%N; SFX(sfx_select);} 
        if(pad_press&BUTTON_DOWN){ s=(s+1)%N; SFX(sfx_select);} 
        if(pad_press&BUTTON_LEFT) d=-1; if(pad_press&BUTTON_RIGHT) d=1;
        if((pad_press&(BUTTON_A|BUTTON_C)) && s!=4) d=1;
        if(d){
            if(s==0) opt_players = opt_players==1 ? 2 : 1;
            if(s==1 && opt_players==1) opt_side^=1;
            if(s==2 && opt_players==1) opt_level=(opt_level+3+d)%3;
            if(s==3) opt_rules^=1;
            if(s!=4) SFX(sfx_move);
        }
        if((pad_press&BUTTON_START) || ((pad_press&(BUTTON_A|BUTTON_C)) && s==4)){ SFX(sfx_promote); break; }
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
static int screen_intro(void){
    fade_out(); reset_screen(); build_pal(pal_board);
    friezes();
    VDP_fillTileMapRect(BG_B, MH, 0,6,40,1); VDP_fillTileMapRect(BG_B, MH, 0,21,40,1);
    big_emblem(0,4,22-14+0); /* placeholder overwritten below */
    clear_a(0,0,40,28);
    text_c("БАТРАХОМИОМАХИЯ",0,40,2,PAL3);
    text_c("по мотивам древнегреческой поэмы",0,40,4,PAL2);
    fade_in();
    for(int p=0;p<5;p++){
        clear_a(1,8,38,12); clear_a(1,23,38,3);
        text_c(intro_pages[p][0],0,40,8,PAL3);
        text_c("A — далее    START — пропустить",0,40,24,PAL2);
        int skip=0;
        for(int l=1;intro_pages[p][l];l++){
            const char *s=intro_pages[p][l];
            u16 n=utf8_len(s); u16 x0=2+(36-n)/2;
            /* typewriter: reveal one character every 2 frames */
            char buf[80]; const char *q=s; int bi=0;
            while(*q){
                do { buf[bi++]=*q++; } while(*q && ((*q&0xC0)==0x80));
                buf[bi]=0;
                if(!skip){ text(buf,x0,9+l*2,PAL2); frame(); frame(); if(pad_press&(BUTTON_A|BUTTON_C)) skip=1; if(pad_press&BUTTON_START){ intro_seen=1; return ST_GAME; } }
            }
            text(s,x0,9+l*2,PAL2);
        }
        for(;;){ frame(); if(pad_press&(BUTTON_A|BUTTON_C)) break; if(pad_press&BUTTON_START){ intro_seen=1; return ST_GAME; } }
        SFX(sfx_select);
    }
    intro_seen=1;
    return ST_GAME;
}

static int screen_game(void){
    fade_out(); reset_screen(); load_game_tiles(); build_pal(pal_board);
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
            panel_msg("Думаю…",0); frame(); frame();
            int i = ai_choose(&pos,opt_rules,opt_level,legal,nlegal);
            /* keep a minimum pause so the move is visible */
            frame();
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
            if(r==2) return ST_MENU;
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
    return ST_END;
}

static int screen_end(void){
    fade_out(); reset_screen(); build_pal(pal_board);
    friezes();
    VDP_fillTileMapRect(BG_B, MH, 0,13,40,1);
    const char *head, *l1, *l2;
    int human_won = opt_players==1 && result==opt_side;
    if(result==2){ head="НИЧЬЯ"; l1="Зевс послал на поле раков —"; l2="и война окончилась за один день."; music(3); }
    else {
        if(opt_players==2) head = result==SIDE_MICE ? "ПОБЕДИЛИ МЫШИ!" : "ПОБЕДИЛИ ЛЯГУШКИ!";
        else head = human_won ? "ПОБЕДА!" : "ПОРАЖЕНИЕ";
        if(result==SIDE_MICE){ l1="Психарпакс отомщён!"; l2="Мыши пируют на берегу пруда."; }
        else { l1="Физигнат удержал свой пруд!"; l2="Лягушки славят царя кваканьем."; }
        music(opt_players==1 && !human_won ? 3 : 2);
        big_emblem(result,16,3);
    }
    if(result==2){ big_emblem(0,10,3); big_emblem(1,22,3); }
    text_c(head,0,40,15,PAL3);
    text_c(l1,0,40,18,PAL2); text_c(l2,0,40,20,PAL2);
    {
        char b[40]; strcpy(b,"Ходов сделано: "); num(b+strlen(b),(nhist+1)/2); text_c(b,0,40,22,PAL2);
    }
    text_c("A — реванш    START — меню",0,40,25,PAL3);
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
