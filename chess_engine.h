// chess_engine.h
#ifndef CHESS_ENGINE_H
#define CHESS_ENGINE_H

#include <stdbool.h>

typedef struct {
    char board[8][8]; // rank 0 = 8th rank, rank 7 = 1st rank (same orientation as previous client)
    bool white_to_move;
    // castling rights
    bool white_king_moved, black_king_moved;
    bool white_rook_a_moved, white_rook_h_moved;
    bool black_rook_a_moved, black_rook_h_moved;
    // en-passant target square as file(0-7)/rank(0-7) or -1 if none (target square behind pawn that moved 2)
    int ep_target_file, ep_target_rank;
} GameState;

/* Initialize a new starting position (standard chess) */
void ge_init(GameState *g);

/* Pretty-print board to stdout */
void ge_print(const GameState *g);

/* Parse a 4-character move string like "e2e4" or 5-char promotion like "e7e8q" */
/* Returns true if the move string parsed and was legal for the side to move.
   If legal, the move is applied to the game state and true returned.
   If illegal, no change is applied and false returned.
*/
bool ge_try_move(GameState *g, const char *mvstr);

/* Apply a move string without legality checking (helper if you trust source) */
bool ge_apply_move_unchecked(GameState *g, const char *mvstr);

/* Return true if the given side is currently in check (true = white) */
bool ge_in_check(const GameState *g, bool white);

/* Return true if the given side has at least one legal move */
bool ge_has_any_legal_move(GameState *g, bool white);

/* Convert algebraic file/rank to indices: file 'a'..'h' -> 0..7, rank '1'..'8' -> 7..0 */
int ge_filechar_to_col(char f);
int ge_rankchar_to_row(char r);

#endif // CHESS_ENGINE_H
