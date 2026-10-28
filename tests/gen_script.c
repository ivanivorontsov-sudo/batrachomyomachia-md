/* host helper: plays a deterministic game (AI vs AI) and prints moves as "from p1 p2 ..." for emulator scripts */
#include <stdio.h>
#include <stdlib.h>
#include "../src/rules.h"
#include "../src/ai.h"
static long nodes_budget;
void ai_time_start(int l){ nodes_budget = ai_nodes + 20000; }
int ai_time_up(void){ return ai_nodes > nodes_budget; }
static unsigned seed=12345; unsigned ai_rand(void){ seed=seed*1103515245+12345; return (seed>>16)&0x7fff; }
int main(int argc,char**argv){
    int rules=argc>1?atoi(argv[1]):0; seed=argc>2?atoi(argv[2]):1;
    int lv0=argc>3?atoi(argv[3]):0, lv1=argc>4?atoi(argv[4]):0;
    rules_init(); Pos p; pos_start(&p); Move mv[MAX_MOVES];
    for(int ply=0;ply<300;ply++){
        int n=gen_moves(&p,rules,mv); if(!n){ printf("END %d\n",p.side^1); return 0; }
        if(p.quiet>=60){ printf("END 2\n"); return 0; }
        int i=ai_choose(&p,rules,p.side?lv1:lv0,mv,n);
        printf("%d",mv[i].from); for(int k=0;k<mv[i].n;k++) printf(" %d",mv[i].path[k]);
        printf(" | capt=%d promo=%d\n",mv[i].ncapt,mv[i].promo);
        make_move(&p,&mv[i],rules);
    }
    printf("END ?\n"); return 0;
}
