#include "rules.h"

signed char nb[32][4];
static const signed char DR[4] = {-1,-1, 1, 1};
static const signed char DC[4] = {-1, 1,-1, 1};

int sq_row(int sq){ return sq>>2; }
int sq_col(int sq){ int r=sq>>2; return ((sq&3)<<1) + ((r&1)?0:1); }
int rc_to_sq(int r,int c){ if(r<0||r>7||c<0||c>7) return -1; if(((r+c)&1)==0) return -1; return (r<<2)+(c>>1); }
int popcount32(bb_t x){ int n=0; while(x){ x&=x-1; n++; } return n; }

void rules_init(void){
    for(int s=0;s<32;s++) for(int d=0;d<4;d++) nb[s][d]=(signed char)rc_to_sq(sq_row(s)+DR[d], sq_col(s)+DC[d]);
}

void pos_start(Pos *p){
    p->men[SIDE_FROGS]=0x00000FFFu;  /* rows 0-2 */
    p->men[SIDE_MICE] =0xFFF00000u;  /* rows 5-7 */
    p->kings[0]=p->kings[1]=0; p->side=SIDE_MICE; p->quiet=0;
}

static int promo_row(int side){ return side==SIDE_MICE?0:7; }

/* ---- capture search ---- */
typedef struct { const Pos *p; int rules; Move *out; int n; ru8 side; bb_t enemy, occ; Move cur; } Ctx;

static int king_cont(Ctx *c,int sq,bb_t capt);

static void emit(Ctx *c){ if(c->n<MAX_MOVES){ c->out[c->n]=c->cur; c->out[c->n].to=c->cur.path[c->cur.n-1]; c->n++; } }

static void cap_rec(Ctx *c,int sq,int king,bb_t capt);

static void push(Ctx *c,int land,int victim,int king,bb_t capt,int promoted){
    Move save=c->cur;
    if(c->cur.n<MAX_PATH){ c->cur.path[c->cur.n++]=(ru8)land; }
    c->cur.capt=capt|BIT(victim); c->cur.ncapt++;
    if(promoted) c->cur.promo=1;
    if(promoted && c->rules==RULES_ENGLISH){ emit(c); }   /* English: promotion ends the move */
    else cap_rec(c,land,king||promoted,c->cur.capt);
    c->cur=save;
}

static int king_cont(Ctx *c,int sq,bb_t capt){
    if(c->rules==RULES_ENGLISH){
        for(int d=0;d<4;d++){ int a=nb[sq][d]; if(a<0) continue; int b=nb[a][d]; if(b<0) continue;
            if((c->enemy&BIT(a)) && !(capt&BIT(a)) && !(c->occ&BIT(b))) return 1; }
        return 0;
    }
    for(int d=0;d<4;d++){
        int a=nb[sq][d];
        while(a>=0 && !(c->occ&BIT(a))) a=nb[a][d];
        if(a<0) continue;
        if(!(c->enemy&BIT(a)) || (capt&BIT(a))) continue;
        int b=nb[a][d];
        if(b>=0 && !(c->occ&BIT(b))) return 1;
    }
    return 0;
}

static void cap_rec(Ctx *c,int sq,int king,bb_t capt){
    int found=0;
    if(!king || c->rules==RULES_ENGLISH){
        for(int d=0;d<4;d++){
            if(!king && c->rules==RULES_ENGLISH && ((c->side==SIDE_MICE)?(d>1):(d<2))) continue;
            int a=nb[sq][d]; if(a<0) continue; int b=nb[a][d]; if(b<0) continue;
            if((c->enemy&BIT(a)) && !(capt&BIT(a)) && !(c->occ&BIT(b))){
                found=1;
                int pr = !king && sq_row(b)==promo_row(c->side);
                push(c,b,a,king,capt,pr);
            }
        }
    } else {
        for(int d=0;d<4;d++){
            int a=nb[sq][d];
            while(a>=0 && !(c->occ&BIT(a))) a=nb[a][d];
            if(a<0) continue;
            if(!(c->enemy&BIT(a)) || (capt&BIT(a))) continue;   /* own piece or already-captured piece blocks */
            /* collect landing squares */
            int lands[8], nl=0, b=nb[a][d];
            while(b>=0 && !(c->occ&BIT(b))){ lands[nl++]=b; b=nb[b][d]; }
            if(!nl) continue;
            found=1;
            bb_t nc=capt|BIT(a);
            int anycont=0, cont[8];
            for(int i=0;i<nl;i++){ cont[i]=king_cont(c,lands[i],nc); anycont|=cont[i]; }
            for(int i=0;i<nl;i++) if(!anycont || cont[i]) push(c,lands[i],a,1,capt,0);
        }
    }
    if(!found && c->cur.n>0) emit(c);
}

int gen_moves(const Pos *p,int rules,Move *out){
    Ctx c; c.p=p; c.rules=rules; c.out=out; c.n=0; c.side=p->side;
    int me=p->side, op=me^1;
    bb_t mine=p->men[me]|p->kings[me];
    c.enemy=p->men[op]|p->kings[op];
    for(int s=0;s<32;s++) if(mine&BIT(s)){
        c.occ=(mine|c.enemy)&~BIT(s);           /* mover's origin is free during the move */
        c.cur.from=(ru8)s; c.cur.n=0; c.cur.capt=0; c.cur.promo=0; c.cur.ncapt=0;
        cap_rec(&c,s,(p->kings[me]&BIT(s))!=0,0);
    }
    if(c.n) return c.n;
    bb_t occ=mine|c.enemy;
    for(int s=0;s<32;s++) if(mine&BIT(s)){
        int king=(p->kings[me]&BIT(s))!=0;
        for(int d=0;d<4;d++){
            if(!king && ((me==SIDE_MICE)?(d>1):(d<2))) continue;
            int a=nb[s][d];
            while(a>=0 && !(occ&BIT(a))){
                if(c.n<MAX_MOVES){ Move *m=&out[c.n++]; m->from=(ru8)s; m->to=(ru8)a; m->n=1; m->path[0]=(ru8)a; m->capt=0; m->ncapt=0;
                    m->promo=(!king && sq_row(a)==promo_row(me)); }
                if(!king || rules==RULES_ENGLISH) break;
                a=nb[a][d];
            }
        }
    }
    return c.n;
}

void make_move(Pos *p,const Move *m,int rules){
    int me=p->side, op=me^1; bb_t f=BIT(m->from), t=BIT(m->to);
    int king=(p->kings[me]&f)!=0;
    p->men[me]&=~f; p->kings[me]&=~f;
    if(king||m->promo) p->kings[me]|=t; else p->men[me]|=t;
    p->men[op]&=~m->capt; p->kings[op]&=~m->capt;
    if(m->capt || !king) p->quiet=0; else if(p->quiet<255) p->quiet++;
    p->side=(ru8)op; (void)rules;
}

int pos_same(const Pos *a,const Pos *b){
    return a->side==b->side && a->men[0]==b->men[0] && a->men[1]==b->men[1] && a->kings[0]==b->kings[0] && a->kings[1]==b->kings[1];
}
int is_threefold(const Pos *hist,int n,const Pos *cur){
    int cnt=1;
    /* only positions after the last capture / man move can repeat */
    for(int i=n-1, k=0; i>=0 && k<cur->quiet; i--, k++){
        if(pos_same(&hist[i],cur) && ++cnt>=3) return 1;
    }
    return 0;
}
