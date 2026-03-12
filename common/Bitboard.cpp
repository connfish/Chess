#include "BitBoard.hpp"

namespace BB {

U64 KnightAttacks[64];
U64 KingAttacks[64];
U64 PawnAttacks[2][64];

static U64 rayAttack(Square s, int dFile, int dRank, U64 occupied) {
    U64 attacks = 0;
    int f = fileOf(s), r = rankOf(s);
    while (true) {
        f += dFile; r += dRank;
        if (f < 0 || f > 7 || r < 0 || r > 7) break;
        Square sq = makeSquare(f, r);
        attacks |= bit(sq);
        if (occupied & bit(sq)) break;
    }
    return attacks;
}

U64 bishopAttacks(Square s, U64 occupied) {
    return rayAttack(s, 1, 1, occupied) | rayAttack(s, 1, -1, occupied) |
           rayAttack(s, -1, 1, occupied) | rayAttack(s, -1, -1, occupied);
}

U64 rookAttacks(Square s, U64 occupied) {
    return rayAttack(s, 1, 0, occupied) | rayAttack(s, -1, 0, occupied) |
           rayAttack(s, 0, 1, occupied) | rayAttack(s, 0, -1, occupied);
}

void initBitboards() {
    const int knightDirs[8][2] = {{-2,-1},{-2,1},{-1,-2},{-1,2},{1,-2},{1,2},{2,-1},{2,1}};
    const int kingDirs[8][2] = {{-1,-1},{-1,0},{-1,1},{0,-1},{0,1},{1,-1},{1,0},{1,1}};

    for (int sq = 0; sq < 64; sq++) {
        int f = fileOf(Square(sq)), r = rankOf(Square(sq));

        KnightAttacks[sq] = 0;
        for (auto& d : knightDirs) {
            int nf = f + d[0], nr = r + d[1];
            if (nf >= 0 && nf < 8 && nr >= 0 && nr < 8)
                KnightAttacks[sq] |= bit(makeSquare(nf, nr));
        }

        KingAttacks[sq] = 0;
        for (auto& d : kingDirs) {
            int nf = f + d[0], nr = r + d[1];
            if (nf >= 0 && nf < 8 && nr >= 0 && nr < 8)
                KingAttacks[sq] |= bit(makeSquare(nf, nr));
        }

        // White pawn attacks
        PawnAttacks[WHITE][sq] = 0;
        if (f > 0 && r < 7) PawnAttacks[WHITE][sq] |= bit(makeSquare(f - 1, r + 1));
        if (f < 7 && r < 7) PawnAttacks[WHITE][sq] |= bit(makeSquare(f + 1, r + 1));

        // Black pawn attacks
        PawnAttacks[BLACK][sq] = 0;
        if (f > 0 && r > 0) PawnAttacks[BLACK][sq] |= bit(makeSquare(f - 1, r - 1));
        if (f < 7 && r > 0) PawnAttacks[BLACK][sq] |= bit(makeSquare(f + 1, r - 1));
    }
}

} // namespace BB