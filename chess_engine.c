// chess_engine.c
#include "chess_engine.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

static bool inside(int r,int c){ return r>=0 && r<8 && c>=0 && c<8; }

int ge_filechar_to_col(char f){ return f - 'a'; }
int ge_rankchar_to_row(char r){ return 8 - (r - '0'); } // '1'->7, '8'->0

void ge_init(GameState *g){
    const char *rows[] = {
        "rnbqkbnr",
        "pppppppp",
        "........",
        "........",
        "........",
        "........",
        "PPPPPPPP",
        "RNBQKBNR"
    };
    for(int r=0;r<8;r++) for(int c=0;c<8;c++) g->board[r][c] = rows[r][c];
    g->white_to_move = true;
    g->white_king_moved = g->black_king_moved = false;
    g->white_rook_a_moved = g->white_rook_h_moved = false;
    g->black_rook_a_moved = g->black_rook_h_moved = false;
    g->ep_target_file = g->ep_target_rank = -1;
}

void ge_print(const GameState *g){
    printf("\n  a b c d e f g h\n");
    for(int r=0;r<8;r++){
        printf("%d ", 8-r);
        for(int c=0;c<8;c++){
            char ch = g->board[r][c];
            if(ch=='.') printf(". ");
            else printf("%c ", ch);
        }
        printf("\n");
    }
    printf("\n");
}

/* Helper: find king position for color */
static void find_king(const GameState *g, bool white, int *out_r, int *out_c){
    char k = white ? 'K' : 'k';
    for(int r=0;r<8;r++) for(int c=0;c<8;c++) if(g->board[r][c]==k){ *out_r=r; *out_c=c; return;}
    *out_r = *out_c = -1;
}

/* Helper: is square attacked by opponent color? */
static bool square_attacked_by(const GameState *g, int row, int col, bool by_white){
    // scan for pawn attacks
    if(by_white){
        int r = row+1; // white pawns attack downward in our board orientation? careful:
        // Our board: row 0 = rank 8 (top), row 7 = rank 1 (bottom)
        // White pawns start on row 6 and move to smaller row indices -> they attack row-1
        // Wait: earlier we used rankchar_to_row: '1' -> 7, '8' -> 0. So white pawns move from higher row (6) to lower row (5), they attack row-1.
        // So white pawn attacks squares (row-1, col-1) and (row-1, col+1).
        r = row - 1;
        if(inside(r,col-1) && g->board[r][col-1]=='P') return true;
        if(inside(r,col+1) && g->board[r][col+1]=='P') return true;
    } else {
        int r = row + 1; // black pawns attack row+1
        if(inside(r,col-1) && g->board[r][col-1]=='p') return true;
        if(inside(r,col+1) && g->board[r][col+1]=='p') return true;
    }

    // knights
    const int knr[8][2] = {{-2,-1},{-2,1},{-1,-2},{-1,2},{1,-2},{1,2},{2,-1},{2,1}};
    for(int i=0;i<8;i++){
        int r = row + knr[i][0], c = col + knr[i][1];
        if(!inside(r,c)) continue;
        char ch = g->board[r][c];
        if(by_white && ch=='N') return true;
        if(!by_white && ch=='n') return true;
    }

    // sliding pieces: bishop/queen diagonals
    const int diag[4][2] = {{-1,-1},{-1,1},{1,-1},{1,1}};
    for(int d=0;d<4;d++){
        int dr = diag[d][0], dc = diag[d][1];
        int r = row + dr, c = col + dc;
        while(inside(r,c)){
            char ch = g->board[r][c];
            if(ch != '.'){
                if(by_white){
                    if(ch=='B' || ch=='Q') return true;
                } else {
                    if(ch=='b' || ch=='q') return true;
                }
                break;
            }
            r += dr; c += dc;
        }
    }

    // sliding pieces: rook/queen orthogonals
    const int ortho[4][2] = {{-1,0},{1,0},{0,-1},{0,1}};
    for(int d=0;d<4;d++){
        int dr = ortho[d][0], dc = ortho[d][1];
        int r = row + dr, c = col + dc;
        while(inside(r,c)){
            char ch = g->board[r][c];
            if(ch != '.'){
                if(by_white){
                    if(ch=='R' || ch=='Q') return true;
                } else {
                    if(ch=='r' || ch=='q') return true;
                }
                break;
            }
            r += dr; c += dc;
        }
    }

    // king adjacent
    for(int dr=-1;dr<=1;dr++) for(int dc=-1;dc<=1;dc++){
        if(dr==0 && dc==0) continue;
        int r = row+dr, c=col+dc;
        if(!inside(r,c)) continue;
        char ch = g->board[r][c];
        if(by_white && ch=='K') return true;
        if(!by_white && ch=='k') return true;
    }

    return false;
}

/* Return true if color is in check */
bool ge_in_check(const GameState *g, bool white){
    int kr,kc;
    find_king(g, white, &kr, &kc);
    if(kr==-1) return false; // shouldn't happen
    return square_attacked_by(g, kr, kc, !white);
}

/* Helper to clone game state */
static void clone_state(const GameState *src, GameState *dst){
    memcpy(dst, src, sizeof(GameState));
}

/* Apply a move without legality checking.
   mvstr is like "e2e4" or "e7e8q" (promotion char optional, lower or upper).
   Returns true on success.
*/
bool ge_apply_move_unchecked(GameState *g, const char *mvstr){
    if(strlen(mvstr) < 4) return false;
    int c0 = ge_filechar_to_col(mvstr[0]);
    int r0 = ge_rankchar_to_row(mvstr[1]);
    int c1 = ge_filechar_to_col(mvstr[2]);
    int r1 = ge_rankchar_to_row(mvstr[3]);
    if(!inside(r0,c0) || !inside(r1,c1)) return false;

    char piece = g->board[r0][c0];
    if(piece == '.') return false;

    // track castling/king/rook moves
    // If moving king update king moved flags
    if(piece == 'K') g->white_king_moved = true;
    if(piece == 'k') g->black_king_moved = true;
    // rook a/h files
    if(piece == 'R'){
        if(r0==7 && c0==0) g->white_rook_a_moved = true;
        if(r0==7 && c0==7) g->white_rook_h_moved = true;
    }
    if(piece == 'r'){
        if(r0==0 && c0==0) g->black_rook_a_moved = true;
        if(r0==0 && c0==7) g->black_rook_h_moved = true;
    }

    // en-passant capture detection: if pawn moves to ep_target square and there is no piece at destination,
    // capture the pawn behind.
    if(tolower(piece)=='p' && g->ep_target_file==c1 && g->ep_target_rank==r1 && g->board[r1][c1]=='.'){
        // capture pawn that moved two last turn: for white moving, captured pawn is at r1+1
        if(piece=='P') g->board[r1+1][c1] = '.';
        else g->board[r1-1][c1] = '.';
    }

    // castling move: king moves two squares horizontally
    if(piece == 'K' && r0==7 && c0==4 && r1==7 && (c1==6 || c1==2)){
        // kingside
        if(c1==6){
            // move rook from h to f
            g->board[7][5] = 'R';
            g->board[7][7] = '.';
            g->white_rook_h_moved = true;
        } else {
            // queenside: rook from a to d
            g->board[7][3] = 'R';
            g->board[7][0] = '.';
            g->white_rook_a_moved = true;
        }
    }
    if(piece == 'k' && r0==0 && c0==4 && r1==0 && (c1==6 || c1==2)){
        if(c1==6){
            g->board[0][5] = 'r';
            g->board[0][7] = '.';
            g->black_rook_h_moved = true;
        } else {
            g->board[0][3] = 'r';
            g->board[0][0] = '.';
            g->black_rook_a_moved = true;
        }
    }

    // move piece
    g->board[r1][c1] = g->board[r0][c0];
    g->board[r0][c0] = '.';

    // promotion: if move has extra char or pawn reached last rank
    if(strlen(mvstr) >= 5){
        char promo = mvstr[4];
        if(promo){
            if(g->board[r1][c1]=='P') g->board[r1][c1] = toupper(promo);
            if(g->board[r1][c1]=='p') g->board[r1][c1] = tolower(promo);
        }
    } else {
        if(g->board[r1][c1]=='P' && r1==0) g->board[r1][c1] = 'Q';
        if(g->board[r1][c1]=='p' && r1==7) g->board[r1][c1] = 'q';
    }

    // update en-passant target: if pawn moved two squares, set target behind it
    g->ep_target_file = -1; g->ep_target_rank = -1;
    if(piece=='P' && r0==6 && r1==4){
        g->ep_target_file = c1; g->ep_target_rank = 5; // square that can be captured into by black pawn
    } else if(piece=='p' && r0==1 && r1==3){
        g->ep_target_file = c1; g->ep_target_rank = 2;
    }

    // flip side to move
    g->white_to_move = !g->white_to_move;
    return true;
}

/* Generate whether a specific pseudo-legal move (ignoring checks) is valid for side color */
static bool pseudo_legal(const GameState *g, int r0,int c0,int r1,int c1, char promotion){
    if(!inside(r0,c0) || !inside(r1,c1)) return false;
    char piece = g->board[r0][c0];
    if(piece=='.') return false;
    bool is_white = (piece>='A' && piece<='Z');
    bool side = g->white_to_move;
    if(is_white != side) return false;

    char dest = g->board[r1][c1];
    if(dest != '.'){
        bool dest_white = (dest>='A' && dest<='Z');
        if(dest_white == is_white) return false; // cannot capture own
    }

    int dr = r1 - r0, dc = c1 - c0;
    char p = tolower(piece);

    if(p=='p'){
        if(is_white){
            // white pawns move up (towards smaller row index)
            if(dc==0 && dr==-1 && dest=='.') return true;
            if(dc==0 && dr==-2 && r0==6 && g->board[5][c0]=='.' && dest=='.') return true;
            if(abs(dc)==1 && dr==-1 && ((dest!='.' && islower(dest)) || (g->ep_target_file==c1 && g->ep_target_rank==r1))) return true;
            return false;
        } else {
            // black pawns move down (towards larger row index)
            if(dc==0 && dr==1 && dest=='.') return true;
            if(dc==0 && dr==2 && r0==1 && g->board[2][c0]=='.' && dest=='.') return true;
            if(abs(dc)==1 && dr==1 && ((dest!='.' && isupper(dest)) || (g->ep_target_file==c1 && g->ep_target_rank==r1))) return true;
            return false;
        }
    } else if(p=='n'){
        if((abs(dr)==2 && abs(dc)==1) || (abs(dr)==1 && abs(dc)==2)) return true;
        return false;
    } else if(p=='b'){
        if(abs(dr)==abs(dc) && dr!=0) {
            int sdr = (dr>0)?1:-1, sdc=(dc>0)?1:-1;
            int r=r0+sdr, c=c0+sdc;
            while(r!=r1 && c!=c1){
                if(g->board[r][c] != '.') return false;
                r+=sdr; c+=sdc;
            }
            return true;
        }
        return false;
    } else if(p=='r'){
        if((dr==0) ^ (dc==0)){
            int sdr = (dr==0)?0:((dr>0)?1:-1);
            int sdc = (dc==0)?0:((dc>0)?1:-1);
            int r=r0+sdr, c=c0+sdc;
            while(r!=r1 || c!=c1){
                if(g->board[r][c] != '.') return false;
                r += sdr; c += sdc;
            }
            return true;
        }
        return false;
    } else if(p=='q'){
        if((abs(dr)==abs(dc) && dr!=0) || ((dr==0) ^ (dc==0))){
            int sdr = (dr==0)?0:((dr>0)?1:-1);
            int sdc = (dc==0)?0:((dc>0)?1:-1);
            int r=r0+sdr, c=c0+sdc;
            while(r!=r1 || c!=c1){
                if(g->board[r][c] != '.') return false;
                r += sdr; c += sdc;
            }
            return true;
        }
        return false;
    } else if(p=='k'){
        if(abs(dr)<=1 && abs(dc)<=1) return true;
        // castling
        if(is_white && r0==7 && c0==4 && r1==7 && (c1==6 || c1==2)){
            // check rook status and empties and not in check and not passing through attacked squares
            if(g->white_king_moved) return false;
            if(c1==6){
                if(g->white_rook_h_moved) return false;
                if(g->board[7][5] != '.' || g->board[7][6] != '.') return false;
                // king not currently in check, squares f1 and g1 not attacked
                if(square_attacked_by(g,7,4,false)) return false;
                if(square_attacked_by(g,7,5,false)) return false;
                if(square_attacked_by(g,7,6,false)) return false;
                return true;
            } else {
                if(g->white_rook_a_moved) return false;
                if(g->board[7][1] != '.' || g->board[7][2] != '.' || g->board[7][3] != '.') return false;
                if(square_attacked_by(g,7,4,false)) return false;
                if(square_attacked_by(g,7,3,false)) return false;
                if(square_attacked_by(g,7,2,false)) return false;
                return true;
            }
        }
        if(!is_white && r0==0 && c0==4 && r1==0 && (c1==6 || c1==2)){
            if(g->black_king_moved) return false;
            if(c1==6){
                if(g->black_rook_h_moved) return false;
                if(g->board[0][5] != '.' || g->board[0][6] != '.') return false;
                if(square_attacked_by(g,0,4,true)) return false;
                if(square_attacked_by(g,0,5,true)) return false;
                if(square_attacked_by(g,0,6,true)) return false;
                return true;
            } else {
                if(g->black_rook_a_moved) return false;
                if(g->board[0][1] != '.' || g->board[0][2] != '.' || g->board[0][3] != '.') return false;
                if(square_attacked_by(g,0,4,true)) return false;
                if(square_attacked_by(g,0,3,true)) return false;
                if(square_attacked_by(g,0,2,true)) return false;
                return true;
            }
        }
        return false;
    }
    return false;
}

/* Try move with legal checking: simulate and ensure side is not left in check */
bool ge_try_move(GameState *g, const char *mvstr){
    if(strlen(mvstr) < 4) return false;
    int c0 = ge_filechar_to_col(mvstr[0]);
    int r0 = ge_rankchar_to_row(mvstr[1]);
    int c1 = ge_filechar_to_col(mvstr[2]);
    int r1 = ge_rankchar_to_row(mvstr[3]);
    if(!inside(r0,c0) || !inside(r1,c1)) return false;

    char promotion = 0;
    if(strlen(mvstr) >= 5) promotion = mvstr[4];

    // quick validation: piece exists and belongs to side to move
    char piece = g->board[r0][c0];
    if(piece=='.') return false;
    bool is_white = (piece>='A' && piece<='Z');
    if(is_white != g->white_to_move) return false;

    if(!pseudo_legal(g, r0,c0,r1,c1,promotion)) return false;

    // simulate
    GameState copy;
    clone_state(g, &copy);
    // To simulate properly for castling/en-passant/promotion, reuse apply_unchecked
    // but we need to apply with a constructed mvstr that includes promotion if present
    char buf[8];
    if(promotion) snprintf(buf,sizeof(buf),"%c%c%c%c%c", mvstr[0],mvstr[1],mvstr[2],mvstr[3],promotion);
    else snprintf(buf,sizeof(buf),"%c%c%c%c", mvstr[0],mvstr[1],mvstr[2],mvstr[3]);
    ge_apply_move_unchecked(&copy, buf);

    // If side is in check after move, illegal
    if(ge_in_check(&copy, is_white)) return false;

    // otherwise commit
    ge_apply_move_unchecked(g, buf);
    return true;
}

/* Has any legal move for given color? We'll iterate all squares and try pseudo-legal moves then final legal check */
bool ge_has_any_legal_move(GameState *g, bool white){
    // ensure side to move matches requested; if not, temporarily flip
    bool orig_turn = g->white_to_move;
    g->white_to_move = white;
    for(int r0=0;r0<8;r0++) for(int c0=0;c0<8;c0++){
        char p = g->board[r0][c0];
        if(p=='.') continue;
        bool p_white = (p>='A' && p<='Z');
        if(p_white != white) continue;
        // try all destinations
        for(int r1=0;r1<8;r1++) for(int c1=0;c1<8;c1++){
            char testmv[6];
            testmv[0] = 'a' + c0;
            testmv[1] = '1' + (7 - r0);
            testmv[2] = 'a' + c1;
            testmv[3] = '1' + (7 - r1);
            testmv[4] = 0;
            testmv[5] = 0;
            if(pseudo_legal(g, r0,c0,r1,c1,0)){
                GameState copy; clone_state(g, &copy);
                ge_apply_move_unchecked(&copy, testmv);
                if(!ge_in_check(&copy, white)){
                    g->white_to_move = orig_turn;
                    return true;
                }
            }
        }
    }
    g->white_to_move = orig_turn;
    return false;
}
