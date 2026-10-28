#ifndef AI_H
#define AI_H
#include "rules.h"
/* level: 0 easy, 1 medium, 2 hard. Returns index into moves[] (n = count). */
int ai_choose(const Pos *p,int rules,int level,const Move *moves,int n);
int ai_eval(const Pos *p,int rules);           /* from the side-to-move perspective */
extern rs32 ai_nodes;
extern int  ai_depth_reached;
/* platform hooks */
int  ai_time_up(void);       /* returns non-zero when the thinking budget is exhausted */
void ai_time_start(int level);
unsigned ai_rand(void);
#endif
