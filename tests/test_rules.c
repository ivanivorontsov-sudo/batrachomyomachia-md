#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "../src/rules.h"
#include "../src/ai.h"
static clock_t t0; static double budget;
int ai_time_up(void){ return (double)(clock()-t0)/CLOCKS_PER_SEC > budget; }
void ai_time_start(int l){ t0=clock(); budget = l==2? 0.25 : 5.0; }  /* host is ~100x faster than a 68000 */
unsigned ai_rand(void){ return (unsigned)rand(); }

static int fails=0, checks=0;
#define CHECK(c,msg) do{ checks++; if(!(c)){ fails++; printf("FAIL: %s (line %d)\n",msg,__LINE__);} }while(0)

static long perft(Pos *p,int rules,int d){
    Move m[MAX_MOVES]; int n=gen_moves(p,rules,m); if(d==1) return n;
    long t=0; for(int i=0;i<n;i++){ Pos q=*p; make_move(&q,&m[i],rules); t+=perft(&q,rules,d-1);} return t;
}
static int S(const char *alg){ int c=alg[0]-'a', r=8-(alg[1]-'0'); return rc_to_sq(r,c); }
static void clr(Pos *p){ memset(p,0,sizeof *p); }
static void put(Pos *p,int side,int king,const char *a){ if(king) p->kings[side]|=BIT(S(a)); else p->men[side]|=BIT(S(a)); }

int main(void){
    rules_init();
    Pos p; pos_start(&p);
    /* English checkers perft reference values (published): 7,49,302,1469,7361,36768,179740,845931 */
    long ref[]={0,7,49,302,1469,7361,36768,179740,845931};
    for(int d=1;d<=8;d++){ long v=perft(&p,RULES_ENGLISH,d); printf("perft EN d%d = %ld (ref %ld)\n",d,v,ref[d]); CHECK(v==ref[d],"english perft"); }
    for(int d=1;d<=7;d++){ long v=perft(&p,RULES_RUSSIAN,d); printf("perft RU d%d = %ld\n",d,v); }

    Move m[MAX_MOVES]; int n;
    /* 1. man captures backwards in Russian, not in English */
    clr(&p); put(&p,0,0,"d4"); put(&p,1,0,"c3"); p.side=0;
    n=gen_moves(&p,RULES_RUSSIAN,m); CHECK(n==1 && m[0].to==S("b2") && m[0].ncapt==1,"RU man captures backwards");
    n=gen_moves(&p,RULES_ENGLISH,m); CHECK(n>0 && m[0].ncapt==0,"EN man cannot capture backwards");
    /* 2. mandatory capture */
    clr(&p); put(&p,0,0,"c3"); put(&p,0,0,"g3"); put(&p,1,0,"d4"); p.side=0;
    n=gen_moves(&p,RULES_RUSSIAN,m); CHECK(n==1 && m[0].from==S("c3") && m[0].to==S("e5"),"capture is mandatory");
    /* 3. flying king: long move and capture with choice of landing */
    clr(&p); put(&p,0,1,"a1"); put(&p,1,0,"d4"); p.side=0;
    n=gen_moves(&p,RULES_RUSSIAN,m); CHECK(n==4,"flying king lands on e5,f6,g7,h8");
    clr(&p); put(&p,0,1,"a1"); p.side=0; n=gen_moves(&p,RULES_RUSSIAN,m); CHECK(n==7,"flying king quiet moves");
    n=gen_moves(&p,RULES_ENGLISH,m); CHECK(n==1,"english king moves one square");
    /* 4. king must land where capture continues */
    clr(&p); put(&p,0,1,"a1"); put(&p,1,0,"c3"); put(&p,1,0,"f6"); p.side=0;
    n=gen_moves(&p,RULES_RUSSIAN,m); { int ok=n==4; for(int i=0;i<n;i++) if(m[i].ncapt!=2) ok=0; CHECK(ok,"king continues capturing on same line (a1:c3:f6 -> g7/h8, 4 paths)"); }
    clr(&p); put(&p,0,1,"a1"); put(&p,1,0,"c3"); put(&p,1,0,"g5"); p.side=0;   /* after b2..d4/e5/... : from e5? f4? */
    n=gen_moves(&p,RULES_RUSSIAN,m);
    { int ok=1; for(int i=0;i<n;i++) if(m[i].ncapt!=2) ok=0; CHECK(n>0 && ok,"must choose landing square that continues capture"); }
    /* 5. promotion during capture continues as king (Russian) / ends move (English) */
        clr(&p); put(&p,0,0,"b6"); put(&p,1,0,"c7"); put(&p,1,0,"f6"); p.side=0;
    n=gen_moves(&p,RULES_RUSSIAN,m); { int ok=n==2; for(int i=0;i<n;i++) if(m[i].ncapt!=2||!m[i].promo) ok=0; CHECK(ok,"RU: b6:d8 promotes and continues as king d8:f6 -> g5/h4"); }
    n=gen_moves(&p,RULES_ENGLISH,m); CHECK(n==1 && m[0].ncapt==1 && m[0].promo && m[0].to==S("d8"),"EN: promotion ends the move");
    /* 6. Turkish strike: captured pieces stay until the end of the move and cannot be jumped twice */
    clr(&p); put(&p,0,1,"a1"); put(&p,1,0,"c3"); put(&p,1,0,"e5"); put(&p,1,0,"f4"); put(&p,1,0,"d6"); p.side=0;
    n=gen_moves(&p,RULES_RUSSIAN,m);
    { int ok=1; for(int i=0;i<n;i++){ if(popcount32(m[i].capt)!=m[i].ncapt) ok=0; } CHECK(n>0&&ok,"no piece captured twice"); }
    /* 7. win detection: side with no moves */
    clr(&p); put(&p,0,0,"a1"); put(&p,1,0,"b2"); put(&p,1,0,"c3"); p.side=0;
    n=gen_moves(&p,RULES_RUSSIAN,m); CHECK(n==0,"blocked side has no moves (loses)");

    /* 8. random self-play: AI always picks legal moves, games terminate */
    srand(1);
    int results[3]={0,0,0};
    for(int g=0; g<24; g++){
        int rules=g&1; Pos q; pos_start(&q); int ply=0;
        while(ply<300){
            n=gen_moves(&q,rules,m); if(n==0){ results[q.side^1]++; break; }
            if(q.quiet>=60){ results[2]++; break; }
            int lvl = (q.side==0)? (g%3) : ((g/3)%3);
            int i=ai_choose(&q,rules,lvl,m,n);
            CHECK(i>=0 && i<n,"ai picks a legal move index");
            Pos chk=q; make_move(&q,&m[i],rules);
            CHECK(popcount32(q.men[0]|q.men[1]|q.kings[0]|q.kings[1])==popcount32(chk.men[0]|chk.men[1]|chk.kings[0]|chk.kings[1])-m[i].ncapt,"piece count consistent");
            CHECK(((q.men[0]|q.kings[0])&(q.men[1]|q.kings[1]))==0,"no overlapping pieces");
            ply++;
        }
        if(ply>=300) results[2]++;
    }
    printf("self-play 24 games: mice %d, frogs %d, draws %d\n",results[0],results[1],results[2]);
    /* 9. strength: hard beats easy */
    int hw=0,hl=0,hd=0;
    for(int g=0; g<20; g++){
        int rules=RULES_RUSSIAN; Pos q; pos_start(&q); int ply=0; int hard_side=g&1; int res=-1;
        while(ply<300){
            n=gen_moves(&q,rules,m); if(n==0){ res=q.side^1; break; }
            if(q.quiet>=60) break;
            int i=ai_choose(&q,rules,q.side==hard_side?2:0,m,n); make_move(&q,&m[i],rules); ply++;
        }
        if(res==hard_side) hw++; else if(res<0) hd++; else hl++;
    }
    printf("hard vs easy (20 games): hard wins %d, draws %d, losses %d\n",hw,hd,hl);
    CHECK(hw>=14,"hard AI clearly stronger than easy");
    printf("\n%d checks, %d failures\n",checks,fails);
    return fails?1:0;
}
