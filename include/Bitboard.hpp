#pragma once
#include "Types.hpp"
 
namespace BB {
 
// File and rank masks
constexpr U64 FileA = 0x0101010101010101ULL;
constexpr U64 FileB = FileA << 1;
constexpr U64 FileG = FileA << 6;
constexpr U64 FileH = FileA << 7;
constexpr U64 Rank1 = 0xFFULL;
constexpr U64 Rank2 = Rank1 << 8;
constexpr U64 Rank4 = Rank1 << 24;
constexpr U64 Rank5 = Rank1 << 32;
constexpr U64 Rank7 = Rank1 << 48;
constexpr U64 Rank8 = Rank1 << 56;
 
inline U64 bit(Square s) { return 1ULL << s; }
inline bool test(U64 bb, Square s) { return (bb >> s) & 1; }
inline void set(U64& bb, Square s) { bb |= bit(s); }
inline void clear(U64& bb, Square s) { bb &= ~bit(s); }
 
inline int popcount(U64 bb) { return __builtin_popcountll(bb); }
inline Square lsb(U64 bb) { return Square(__builtin_ctzll(bb)); }
inline Square popLsb(U64& bb) {
    Square s = lsb(bb);
    bb &= bb - 1;
    return s;
}
 
// Shift helpers
inline U64 shiftN(U64 bb)  { return bb << 8; }
inline U64 shiftS(U64 bb)  { return bb >> 8; }
inline U64 shiftE(U64 bb)  { return (bb & ~FileH) << 1; }
inline U64 shiftW(U64 bb)  { return (bb & ~FileA) >> 1; }
inline U64 shiftNE(U64 bb) { return (bb & ~FileH) << 9; }
inline U64 shiftNW(U64 bb) { return (bb & ~FileA) << 7; }
inline U64 shiftSE(U64 bb) { return (bb & ~FileH) >> 7; }
inline U64 shiftSW(U64 bb) { return (bb & ~FileA) >> 9; }
 
// Pre-computed attack tables
extern U64 KnightAttacks[64];
extern U64 KingAttacks[64];
extern U64 PawnAttacks[2][64];
 
// Sliding piece attacks (classical approach, no magic bitboards for simplicity)
U64 bishopAttacks(Square s, U64 occupied);
U64 rookAttacks(Square s, U64 occupied);
inline U64 queenAttacks(Square s, U64 occupied) {
    return bishopAttacks(s, occupied) | rookAttacks(s, occupied);
}
 
void initBitboards();
 
} // namespace BB