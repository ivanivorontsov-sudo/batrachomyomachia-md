/* host helper: plays a deterministic game (AI vs AI) and prints moves as "from p1 p2 ..." for emulator scripts */
#include <stdio.h>
#include <stdlib.h>
#include "../src/rules.h"
#include "../src/ai.h"
/* the node budget lives in ai.c, identical to the ROM */
void ai_time_start(int l){ (void)l; }
int ai_time_up(void){ return 0; }
static unsigned seed=12345; unsigned ai_rand(void){ seed=seed*1103515245+12345; return (seed>>16)&0x7fff; }
int main(int argc,char**argv){
    int rules=argc>1?atoi(argv[1]):0; seed=argc>2?atoi(argv[2]):1;
    int lv0=argc>3?atoi(argv[3]):0, lv1=argc>4?atoi(argv[4]):0;
    rules_init(); Pos p; pos_start(&p); Move mv[MAX_MOVES]; static Pos hist[400]; int nh=0;
    for(int ply=0;ply<300;ply++){
        int n=gen_moves(&p,rules,mv);
        if(n && is_threefold(hist,nh,&p)){ printf("END 2 repetition\n"); return 0; }
        if(!n){ printf("END %d\n",p.side^1); return 0; }
        if(p.quiet>=60){ printf("END 2\n"); return 0; }
        if(is_threefold(hist,nh,&p)){ printf("END 2 repetition\n"); return 0; }
        int i=ai_choose(&p,rules,p.side?lv1:lv0,mv,n);
        printf("%d",mv[i].from); for(int k=0;k<mv[i].n;k++) printf(" %d",mv[i].path[k]);
        printf(" | capt=%d promo=%d\n",mv[i].ncapt,mv[i].promo);
        hist[nh++]=p; make_move(&p,&mv[i],rules);
    }
    printf("END ?\n"); return 0;
}
