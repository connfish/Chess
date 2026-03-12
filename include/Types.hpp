#pragma once
#include <cstdint>
#include <string>

using U64 = uint64_t;

enum Color : int { WHITE = 0, BLACK = 1, COLOR_NB = 2 };
enum PieceType : int { NO_PIECE = 0, PAWN = 1, KNIGHT = 2, BISHOP = 3, ROOK = 4, QUEEN = 5, KING = 6, PIECE_TYPE_NB = 7 };

// Square indices: a1=0, b1=1, ... h8=63
enum Square : int {
    A1, B1, C1, D1, E1, F1, G1, H1,
    A2, B2, C2, D2, E2, F2, G2, H2,
    A3, B3, C3, D3, E3, F3, G3, H3,
    A4, B4, C4, D4, E4, F4, G4, H4,
    A5, B5, C5, D5, E5, F5, G5, H5,
    A6, B6, C6, D6, E6, F6, G6, H6,
    A7, B7, C7, D7, E7, F7, G7, H7,
    A8, B8, C8, D8, E8, F8, G8, H8,
    SQ_NONE = 64, SQ_NB = 64
};

inline int fileOf(Square s) { return s & 7; }
inline int rankOf(Square s) { return s >> 3; }
inline Square makeSquare(int file, int rank) { return Square(rank * 8 + file); }

inline Color operator~(Color c) { return Color(c ^ 1); }

// Castling rights as bitflags
enum CastlingRight : int {
    NO_CASTLING = 0,
    WHITE_OO  = 1,
    WHITE_OOO = 2,
    BLACK_OO  = 4,
    BLACK_OOO = 8,
    ALL_CASTLING = 15
};

struct Move {
    Square from;
    Square to;
    PieceType promotion; // NO_PIECE if not a promotion
    bool isCastling;
    bool isEnPassant;

    Move() : from(SQ_NONE), to(SQ_NONE), promotion(NO_PIECE), isCastling(false), isEnPassant(false) {}
    Move(Square f, Square t, PieceType promo = NO_PIECE, bool castle = false, bool ep = false)
        : from(f), to(t), promotion(promo), isCastling(castle), isEnPassant(ep) {}

    bool operator==(const Move& o) const {
        return from == o.from && to == o.to && promotion == o.promotion;
    }
    bool isValid() const { return from != SQ_NONE && to != SQ_NONE; }

    std::string toAlgebraic() const {
        if (!isValid()) return "0000";
        std::string s;
        s += char('a' + fileOf(from));
        s += char('1' + rankOf(from));
        s += char('a' + fileOf(to));
        s += char('1' + rankOf(to));
        if (promotion != NO_PIECE) {
            const char promoChars[] = " pnbrqk";
            s += promoChars[promotion];
        }
        return s;
    }

    static Move fromAlgebraic(const std::string& s) {
        if (s.length() < 4) return Move();
        int ff = s[0] - 'a', fr = s[1] - '1';
        int tf = s[2] - 'a', tr = s[3] - '1';
        if (ff < 0 || ff > 7 || fr < 0 || fr > 7 || tf < 0 || tf > 7 || tr < 0 || tr > 7)
            return Move();
        PieceType promo = NO_PIECE;
        if (s.length() == 5) {
            switch (s[4]) {
                case 'q': promo = QUEEN; break;
                case 'r': promo = ROOK; break;
                case 'b': promo = BISHOP; break;
                case 'n': promo = KNIGHT; break;
                default: break;
            }
        }
        return Move(makeSquare(ff, fr), makeSquare(tf, tr), promo);
    }
};