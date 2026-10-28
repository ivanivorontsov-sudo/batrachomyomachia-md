/* Alpha-beta (negamax) search with iterative deepening, capture extensions,
 * killer-free simple move ordering. Designed for a 7.67 MHz 68000. */
#include "ai.h"

rs32 ai_nodes; int ai_depth_reached;
static int stop_flag;
#define INF 30000
#define WIN 20000

/* positional tables (index by square, from mice point of view: row 0 = promotion row) */
static const signed char ADV[8]  = { 0, 28, 20, 14, 9, 5, 2, 0 };
static const signed char CENTER[32]={
 0,0,0,0,  1,2,2,1,  2,5,5,2,  3,7,7,3,  3,7,7,3,  2,5,5,2,  1,2,2,1,  0,0,0,0 };

int ai_eval(const Pos *p,int rules){
    int sc[2]={0,0};
    int kingval = rules==RULES_RUSSIAN? 290 : 210;
    for(int side=0;side<2;side++){
        bb_t m=p->men[side], k=p->kings[side];
        int s;
        while(m){ s=0; bb_t t=m; while(!(t&1)){t>>=1;s++;} m&=m-1;
            int r=sq_row(s); int adv = side==SIDE_MICE? ADV[r] : ADV[7-r];
            sc[side]+=100+adv+CENTER[s];
            /* back rank guard */
            if((side==SIDE_MICE && r==7) || (side==SIDE_FROGS && r==0)) sc[side]+=8;
        }
        while(k){ s=0; bb_t t=k; while(!(t&1)){t>>=1;s++;} k&=k-1;
            sc[side]+=kingval+CENTER[s]*2;
            /* the long diagonal (a1-h8) is very strong for a flying king */
            if(rules==RULES_RUSSIAN && sq_row(s)+sq_col(s)==7) sc[side]+=20;
        }
    }
    int me=p->side;
    int diff=sc[me]-sc[me^1];
    /* when ahead, trade down */
    int tot=sc[0]+sc[1];
    if(diff>0) diff += (3000-tot)/60; else if(diff<0) diff -= (3000-tot)/60;
    return diff;
}

static Move stack[900];   /* 68000 has 64 KB of RAM; same size on the host so that games replay identically */
/* deterministic thinking budget in nodes (not time): the host replays exactly what the console plays */
static const rs32 node_budget[3]={150,500,1100};
static rs32 budget_now;
static int sp;

static int order_key(const Move *m){ return m->ncapt*16 + m->promo*8; }

static int search(const Pos *p,int rules,int depth,int alpha,int beta,int ply){
    ai_nodes++;
    if((ai_nodes&127)==0 && (ai_nodes>budget_now || ai_time_up())) stop_flag=1;
    if(stop_flag) return 0;
    if(p->quiet>=60) return 0;
    if(sp+MAX_MOVES>=(int)(sizeof(stack)/sizeof(stack[0]))) return ai_eval(p,rules);
    Move *mv=&stack[sp];
    int n=gen_moves(p,rules,mv);
    if(n==0) return -WIN+ply;
    int capt = mv[0].ncapt>0;
    if(depth<=0 && !(capt && ply<24)) return ai_eval(p,rules);
    if(depth<=0) depth=1;          /* capture extension: never stop in the middle of an exchange */
    sp+=n;
    /* simple ordering */
    for(int i=1;i<n;i++){ Move t=mv[i]; int k=order_key(&t); int j=i-1; while(j>=0 && order_key(&mv[j])<k){ mv[j+1]=mv[j]; j--; } mv[j+1]=t; }
    int best=-INF;
    for(int i=0;i<n;i++){
        Pos q=*p; make_move(&q,&mv[i],rules);
        int v=-search(&q,rules,depth-1 + (n==1 && ply<20 ? 1:0),-beta,-alpha,ply+1);
        if(stop_flag) break;
        if(v>best) best=v;
        if(v>alpha) alpha=v;
        if(alpha>=beta) break;
    }
    sp-=n;
    return best;
}

int ai_choose(const Pos *p,int rules,int level,const Move *moves,int n){
    if(n<=1) return 0;
    static const int maxd[3]={2,5,12};
    static const int noise[3]={45,10,0};
    int order[MAX_MOVES]; int score[MAX_MOVES];
    for(int i=0;i<n;i++){ order[i]=i; score[i]=0; }
    ai_time_start(level); stop_flag=0; ai_nodes=0; budget_now=node_budget[level]; ai_depth_reached=0; sp=0;
    int bestidx=0;
    for(int d=1; d<=maxd[level]; d++){
        int alpha=-INF, cur_best=-1, cur_val=-INF;
        int sc[MAX_MOVES];
        for(int k=0;k<n;k++){
            int i=order[k];
            Pos q=*p; make_move(&q,&moves[i],rules);
            int v=-search(&q,rules,d-1,-INF, noise[level]? INF : -alpha, 1);
            if(stop_flag) break;
            if(noise[level]) v += (int)(ai_rand()%(unsigned)(noise[level]*2+1)) - noise[level];
            sc[i]=v;
            if(v>cur_val){ cur_val=v; cur_best=i; }
            if(v>alpha) alpha=v;
        }
        if(stop_flag){ if(d==1 && cur_best>=0) bestidx=cur_best; break; }
        bestidx=cur_best; ai_depth_reached=d;
        for(int k=0;k<n;k++) score[k]=sc[k];
        /* sort order by score desc for next iteration */
        for(int a=1;a<n;a++){ int t=order[a]; int j=a-1; while(j>=0 && score[order[j]]<score[t]){ order[j+1]=order[j]; j--; } order[j+1]=t; }
        if(cur_val>WIN-100 || cur_val<-WIN+100) break;
        if(ai_nodes>budget_now || ai_time_up()) break;
    }
    return bestidx;
}
