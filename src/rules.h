/* Rules engine for Russian (default) and English draughts.
 * Pure C, no SGDK dependency: compiled both for the 68000 ROM and for host unit tests. */
#ifndef RULES_H
#define RULES_H
#ifdef SGDK_GCC
#include <genesis.h>
typedef u32 bb_t; typedef u8 ru8; typedef s16 rs16; typedef s32 rs32;
#else
#include <stdint.h>
typedef uint32_t bb_t; typedef uint8_t ru8; typedef int16_t rs16; typedef int32_t rs32;
#endif

#define RULES_RUSSIAN 0
#define RULES_ENGLISH 1
#define SIDE_MICE  0   /* "white": bottom, moves first, goes up   */
#define SIDE_FROGS 1   /* "black": top, goes down                 */
#define MAX_PATH 12
#define MAX_MOVES 96

typedef struct { bb_t men[2]; bb_t kings[2]; ru8 side; ru8 quiet; } Pos; /* quiet = plies since capture/man move */
typedef struct { ru8 from, to, n, promo, ncapt; ru8 path[MAX_PATH]; bb_t capt; } Move;

extern signed char nb[32][4];      /* neighbour per direction, -1 = off board */
void rules_init(void);
void pos_start(Pos *p);
int  gen_moves(const Pos *p, int rules, Move *out);   /* legal moves, capture is mandatory */
void make_move(Pos *p, const Move *m, int rules);
int  sq_row(int sq);
int  sq_col(int sq);
int  rc_to_sq(int row, int col);                     /* -1 if light square */
int  popcount32(bb_t x);
#define BIT(s) (((bb_t)1)<<(s))
#endif
