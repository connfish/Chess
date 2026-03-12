#include "Board.hpp"
#include <sstream>
#include <algorithm>
#include <cstdlib>
#include <cstring>

Board::Board() { clear(); }

void Board::clear() {
    std::memset(pieces, 0, sizeof(pieces));
    std::memset(occupied, 0, sizeof(occupied));
    allOccupied = 0;
    sideToMove = WHITE;
    castlingRights = NO_CASTLING;
    enPassantSq = SQ_NONE;
    halfMoveClock = 0;
    fullMoveNumber = 1;
    for (int i = 0; i < SQ_NB; i++) {
        pieceOn[i] = NO_PIECE;
        colorOn[i] = WHITE;
    }
}

void Board::setStartPos() {
    clear();
    // White pieces
    putPiece(WHITE, ROOK, A1);   putPiece(WHITE, KNIGHT, B1);
    putPiece(WHITE, BISHOP, C1); putPiece(WHITE, QUEEN, D1);
    putPiece(WHITE, KING, E1);   putPiece(WHITE, BISHOP, F1);
    putPiece(WHITE, KNIGHT, G1); putPiece(WHITE, ROOK, H1);
    for (int f = 0; f < 8; f++) putPiece(WHITE, PAWN, makeSquare(f, 1));
    // Black pieces
    putPiece(BLACK, ROOK, A8);   putPiece(BLACK, KNIGHT, B8);
    putPiece(BLACK, BISHOP, C8); putPiece(BLACK, QUEEN, D8);
    putPiece(BLACK, KING, E8);   putPiece(BLACK, BISHOP, F8);
    putPiece(BLACK, KNIGHT, G8); putPiece(BLACK, ROOK, H8);
    for (int f = 0; f < 8; f++) putPiece(BLACK, PAWN, makeSquare(f, 6));

    sideToMove = WHITE;
    castlingRights = ALL_CASTLING;
    enPassantSq = SQ_NONE;
    halfMoveClock = 0;
    fullMoveNumber = 1;
}

void Board::putPiece(Color c, PieceType pt, Square s) {
    BB::set(pieces[c][pt], s);
    BB::set(occupied[c], s);
    BB::set(allOccupied, s);
    pieceOn[s] = pt;
    colorOn[s] = c;
}

void Board::removePiece(Square s) {
    if (pieceOn[s] == NO_PIECE) return;
    Color c = colorOn[s];
    PieceType pt = pieceOn[s];
    BB::clear(pieces[c][pt], s);
    BB::clear(occupied[c], s);
    BB::clear(allOccupied, s);
    pieceOn[s] = NO_PIECE;
}

void Board::movePiece(Square from, Square to) {
    Color c = colorOn[from];
    PieceType pt = pieceOn[from];
    removePiece(from);
    removePiece(to); // capture
    putPiece(c, pt, to);
}

Square Board::kingSquare(Color c) const {
    U64 k = pieces[c][KING];
    return k ? BB::lsb(k) : SQ_NONE;
}

U64 Board::attackersTo(Square s, U64 occ) const {
    return (BB::PawnAttacks[BLACK][s] & pieces[WHITE][PAWN]) |
           (BB::PawnAttacks[WHITE][s] & pieces[BLACK][PAWN]) |
           (BB::KnightAttacks[s] & (pieces[WHITE][KNIGHT] | pieces[BLACK][KNIGHT])) |
           (BB::bishopAttacks(s, occ) & (pieces[WHITE][BISHOP] | pieces[BLACK][BISHOP] |
                                          pieces[WHITE][QUEEN] | pieces[BLACK][QUEEN])) |
           (BB::rookAttacks(s, occ) & (pieces[WHITE][ROOK] | pieces[BLACK][ROOK] |
                                        pieces[WHITE][QUEEN] | pieces[BLACK][QUEEN])) |
           (BB::KingAttacks[s] & (pieces[WHITE][KING] | pieces[BLACK][KING]));
}

bool Board::isSquareAttacked(Square s, Color by) const {
    return attackersTo(s) & occupied[by];
}

bool Board::isInCheck(Color c) const {
    Square ksq = kingSquare(c);
    return ksq != SQ_NONE && isSquareAttacked(ksq, ~c);
}

bool Board::makeMove(const Move& m) {
    Color us = sideToMove;
    PieceType pt = pieceOn[m.from];
    PieceType captured = pieceOn[m.to];

    // En passant capture
    if (m.isEnPassant) {
        Square capSq = makeSquare(fileOf(m.to), rankOf(m.from));
        removePiece(capSq);
    }

    // Move the piece
    removePiece(m.from);
    removePiece(m.to);

    if (m.promotion != NO_PIECE)
        putPiece(us, m.promotion, m.to);
    else
        putPiece(us, pt, m.to);

    // Castling - move the rook
    if (m.isCastling) {
        if (m.to == G1) { removePiece(H1); putPiece(WHITE, ROOK, F1); }
        else if (m.to == C1) { removePiece(A1); putPiece(WHITE, ROOK, D1); }
        else if (m.to == G8) { removePiece(H8); putPiece(BLACK, ROOK, F8); }
        else if (m.to == C8) { removePiece(A8); putPiece(BLACK, ROOK, D8); }
    }

    // Update en passant square
    enPassantSq = SQ_NONE;
    if (pt == PAWN && std::abs(rankOf(m.to) - rankOf(m.from)) == 2) {
        enPassantSq = makeSquare(fileOf(m.from), (rankOf(m.from) + rankOf(m.to)) / 2);
    }

    // Update castling rights
    if (pt == KING) {
        if (us == WHITE) castlingRights &= ~(WHITE_OO | WHITE_OOO);
        else castlingRights &= ~(BLACK_OO | BLACK_OOO);
    }
    if (m.from == A1 || m.to == A1) castlingRights &= ~WHITE_OOO;
    if (m.from == H1 || m.to == H1) castlingRights &= ~WHITE_OO;
    if (m.from == A8 || m.to == A8) castlingRights &= ~BLACK_OOO;
    if (m.from == H8 || m.to == H8) castlingRights &= ~BLACK_OO;

    // Update clocks
    if (pt == PAWN || captured != NO_PIECE) halfMoveClock = 0;
    else halfMoveClock++;
    if (us == BLACK) fullMoveNumber++;

    sideToMove = (us == WHITE) ? BLACK : WHITE;

    // Check if move left our king in check (illegal)
    if (isInCheck(us)) return false;

    return true;
}

// ---- Move Generation ----

void Board::addPromotions(std::vector<Move>& moves, Square from, Square to) const {
    moves.emplace_back(from, to, QUEEN);
    moves.emplace_back(from, to, ROOK);
    moves.emplace_back(from, to, BISHOP);
    moves.emplace_back(from, to, KNIGHT);
}

void Board::generatePawnMoves(std::vector<Move>& moves, Color us) const {
    U64 pawns = pieces[us][PAWN];
    U64 enemy = occupied[~us];
    U64 promoRank = (us == WHITE) ? BB::Rank8 : BB::Rank1;
    U64 startRank = (us == WHITE) ? BB::Rank2 : BB::Rank7;

    while (pawns) {
        Square s = BB::popLsb(pawns);
        int dir = (us == WHITE) ? 8 : -8;
        Square one = Square(s + dir);

        // Single push
        if (one >= A1 && one <= H8 && pieceOn[one] == NO_PIECE) {
            if (BB::bit(one) & promoRank)
                addPromotions(moves, s, one);
            else {
                moves.emplace_back(s, one);
                // Double push
                if (BB::bit(s) & startRank) {
                    Square two = Square(s + dir * 2);
                    if (pieceOn[two] == NO_PIECE)
                        moves.emplace_back(s, two);
                }
            }
        }

        // Captures
        U64 attacks = BB::PawnAttacks[us][s];
        U64 captures = attacks & enemy;
        while (captures) {
            Square to = BB::popLsb(captures);
            if (BB::bit(to) & promoRank)
                addPromotions(moves, s, to);
            else
                moves.emplace_back(s, to);
        }

        // En passant
        if (enPassantSq != SQ_NONE && (attacks & BB::bit(enPassantSq))) {
            moves.emplace_back(s, enPassantSq, NO_PIECE, false, true);
        }
    }
}

void Board::generateKnightMoves(std::vector<Move>& moves, Color us) const {
    U64 knights = pieces[us][KNIGHT];
    while (knights) {
        Square s = BB::popLsb(knights);
        U64 targets = BB::KnightAttacks[s] & ~occupied[us];
        while (targets) {
            Square to = BB::popLsb(targets);
            moves.emplace_back(s, to);
        }
    }
}

void Board::generateBishopMoves(std::vector<Move>& moves, Color us) const {
    U64 bishops = pieces[us][BISHOP];
    while (bishops) {
        Square s = BB::popLsb(bishops);
        U64 targets = BB::bishopAttacks(s, allOccupied) & ~occupied[us];
        while (targets) moves.emplace_back(s, BB::popLsb(targets));
    }
}

void Board::generateRookMoves(std::vector<Move>& moves, Color us) const {
    U64 rooks = pieces[us][ROOK];
    while (rooks) {
        Square s = BB::popLsb(rooks);
        U64 targets = BB::rookAttacks(s, allOccupied) & ~occupied[us];
        while (targets) moves.emplace_back(s, BB::popLsb(targets));
    }
}

void Board::generateQueenMoves(std::vector<Move>& moves, Color us) const {
    U64 queens = pieces[us][QUEEN];
    while (queens) {
        Square s = BB::popLsb(queens);
        U64 targets = BB::queenAttacks(s, allOccupied) & ~occupied[us];
        while (targets) moves.emplace_back(s, BB::popLsb(targets));
    }
}

void Board::generateKingMoves(std::vector<Move>& moves, Color us) const {
    Square ksq = kingSquare(us);
    if (ksq == SQ_NONE) return;
    U64 targets = BB::KingAttacks[ksq] & ~occupied[us];
    while (targets) moves.emplace_back(ksq, BB::popLsb(targets));
}

void Board::generateCastlingMoves(std::vector<Move>& moves, Color us) const {
    if (isInCheck(us)) return;
    Square ksq = kingSquare(us);

    if (us == WHITE) {
        if ((castlingRights & WHITE_OO) && pieceOn[F1] == NO_PIECE && pieceOn[G1] == NO_PIECE &&
            !isSquareAttacked(F1, BLACK) && !isSquareAttacked(G1, BLACK))
            moves.emplace_back(ksq, G1, NO_PIECE, true);
        if ((castlingRights & WHITE_OOO) && pieceOn[D1] == NO_PIECE && pieceOn[C1] == NO_PIECE &&
            pieceOn[B1] == NO_PIECE && !isSquareAttacked(D1, BLACK) && !isSquareAttacked(C1, BLACK))
            moves.emplace_back(ksq, C1, NO_PIECE, true);
    } else {
        if ((castlingRights & BLACK_OO) && pieceOn[F8] == NO_PIECE && pieceOn[G8] == NO_PIECE &&
            !isSquareAttacked(F8, WHITE) && !isSquareAttacked(G8, WHITE))
            moves.emplace_back(ksq, G8, NO_PIECE, true);
        if ((castlingRights & BLACK_OOO) && pieceOn[D8] == NO_PIECE && pieceOn[C8] == NO_PIECE &&
            pieceOn[B8] == NO_PIECE && !isSquareAttacked(D8, WHITE) && !isSquareAttacked(C8, WHITE))
            moves.emplace_back(ksq, C8, NO_PIECE, true);
    }
}

std::vector<Move> Board::generatePseudoLegalMoves() const {
    std::vector<Move> moves;
    moves.reserve(256);
    Color us = sideToMove;
    generatePawnMoves(moves, us);
    generateKnightMoves(moves, us);
    generateBishopMoves(moves, us);
    generateRookMoves(moves, us);
    generateQueenMoves(moves, us);
    generateKingMoves(moves, us);
    generateCastlingMoves(moves, us);
    return moves;
}

std::vector<Move> Board::generateLegalMoves() const {
    auto pseudoMoves = generatePseudoLegalMoves();
    std::vector<Move> legal;
    legal.reserve(pseudoMoves.size());

    for (const auto& m : pseudoMoves) {
        Board copy = *this;
        if (copy.makeMove(m))
            legal.push_back(m);
    }
    return legal;
}

bool Board::isLegalMove(const Move& m) const {
    Board copy = *this;
    return copy.makeMove(m);
}

Move Board::matchMove(const Move& input) const {
    auto legal = generateLegalMoves();
    for (auto& m : legal) {
        if (m.from == input.from && m.to == input.to &&
            (input.promotion == NO_PIECE || m.promotion == input.promotion)) {
            return m; // returns the legal move with correct flags (castling, ep)
        }
    }
    return Move(); // invalid
}

bool Board::isCheckmate() const {
    return isInCheck(sideToMove) && generateLegalMoves().empty();
}

bool Board::isStalemate() const {
    return !isInCheck(sideToMove) && generateLegalMoves().empty();
}

bool Board::hasInsufficientMaterial() const {
    // K vs K
    if (BB::popcount(allOccupied) == 2) return true;
    // K+B vs K or K+N vs K
    if (BB::popcount(allOccupied) == 3) {
        if (pieces[WHITE][KNIGHT] || pieces[BLACK][KNIGHT]) return true;
        if (pieces[WHITE][BISHOP] || pieces[BLACK][BISHOP]) return true;
    }
    return false;
}

bool Board::isDraw() const {
    if (halfMoveClock >= 100) return true;
    if (hasInsufficientMaterial()) return true;
    return false;
}

std::string Board::toAscii(bool flipped) const {
    const char pieceChars[] = " PNBRQK";
    std::string result;
    result += "\n  +---+---+---+---+---+---+---+---+\n";

    for (int displayRank = 0; displayRank < 8; displayRank++) {
        int r = flipped ? displayRank : (7 - displayRank);
        result += std::to_string(r + 1) + " ";
        for (int displayFile = 0; displayFile < 8; displayFile++) {
            int f = flipped ? (7 - displayFile) : displayFile;
            Square s = makeSquare(f, r);
            result += "| ";
            if (pieceOn[s] != NO_PIECE) {
                char ch = pieceChars[pieceOn[s]];
                if (colorOn[s] == BLACK) ch = ch - 'A' + 'a';
                result += ch;
            } else {
                // Checkerboard pattern with dots
                result += ((f + r) % 2 == 0) ? '.' : ' ';
            }
            result += ' ';
        }
        result += "|\n  +---+---+---+---+---+---+---+---+\n";
    }
    if (flipped)
        result += "    h   g   f   e   d   c   b   a\n";
    else
        result += "    a   b   c   d   e   f   g   h\n";
    return result;
}

std::string Board::toFEN() const {
    const char pieceChars[] = " PNBRQK";
    std::string fen;
    for (int r = 7; r >= 0; r--) {
        int empty = 0;
        for (int f = 0; f < 8; f++) {
            Square s = makeSquare(f, r);
            if (pieceOn[s] == NO_PIECE) { empty++; continue; }
            if (empty) { fen += std::to_string(empty); empty = 0; }
            char ch = pieceChars[pieceOn[s]];
            fen += (colorOn[s] == WHITE) ? ch : char(ch - 'A' + 'a');
        }
        if (empty) fen += std::to_string(empty);
        if (r > 0) fen += '/';
    }
    fen += (sideToMove == WHITE) ? " w " : " b ";
    std::string castle;
    if (castlingRights & WHITE_OO) castle += 'K';
    if (castlingRights & WHITE_OOO) castle += 'Q';
    if (castlingRights & BLACK_OO) castle += 'k';
    if (castlingRights & BLACK_OOO) castle += 'q';
    fen += castle.empty() ? "-" : castle;
    fen += ' ';
    if (enPassantSq != SQ_NONE) {
        fen += char('a' + fileOf(enPassantSq));
        fen += char('1' + rankOf(enPassantSq));
    } else fen += '-';
    fen += ' ' + std::to_string(halfMoveClock) + ' ' + std::to_string(fullMoveNumber);
    return fen;
}