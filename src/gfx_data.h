#ifndef GFX_DATA_H
#define GFX_DATA_H
#include <genesis.h>
extern const u16 pal_board[16];
extern const u16 pal_pieces[16];
extern const u16 pal_text[16];
extern const u16 pal_gold[16];
extern const u32 tiles_pieces[576];
#define TILES_PIECES_N 72
extern const u32 tiles_cursor[216];
#define TILES_CURSOR_N 27
extern const u32 tiles_board[216];
#define TILES_BOARD_N 27
extern const u32 tiles_frame[24];
#define TILES_FRAME_N 3
extern const u32 tiles_column[416];
#define TILES_COLUMN_N 52
extern const u32 tiles_big[1024];
#define TILES_BIG_N 128
extern const u32 tiles_font[1360];
#define TILES_FONT_N 170
#define FONT_CHARS 170
#endif
