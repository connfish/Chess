#pragma once
#include "Types.hpp"
#include "BitBoard.hpp"
#include <vector>
#include <string>

class Board {
public:
    // Bitboards for each piece type per color
    U64 pieces[COLOR_NB][PIECE_TYPE_NB]; // pieces[color][pieceType]
    U64 occupied[COLOR_NB];              // all pieces of each color
    U64 allOccupied;                     // all pieces

    Color sideToMove;
    int castlingRights;
    Square enPassantSq;
    int halfMoveClock;
    int fullMoveNumber;

    // Piece lookup by square
    PieceType pieceOn[SQ_NB];
    Color colorOn[SQ_NB];

    Board();
    void setStartPos();
    void clear();

    // Piece manipulation
    void putPiece(Color c, PieceType pt, Square s);
    void removePiece(Square s);
    void movePiece(Square from, Square to);

    // Move execution
    bool makeMove(const Move& m); // returns false if move leaves king in check
    void unmakeMove(const Move& m, PieceType captured, int prevCastling, Square prevEP, int prevHalfMove);

    // Attack detection
    U64 attackersTo(Square s, U64 occ) const;
    U64 attackersTo(Square s) const { return attackersTo(s, allOccupied); }
    bool isSquareAttacked(Square s, Color by) const;
    bool isInCheck(Color c) const;
    Square kingSquare(Color c) const;

    // Move generation
    std::vector<Move> generateLegalMoves() const;
    std::vector<Move> generatePseudoLegalMoves() const;
    bool isLegalMove(const Move& m) const;

    // Game state
    bool isCheckmate() const;
    bool isStalemate() const;
    bool isDraw() const; // 50-move rule, insufficient material
    bool hasInsufficientMaterial() const;

    // Display
    std::string toAscii(bool flipped = false) const;
    std::string toFEN() const;

    // Validate and match a user move against legal moves
    Move matchMove(const Move& input) const;

private:
    void generatePawnMoves(std::vector<Move>& moves, Color us) const;
    void generateKnightMoves(std::vector<Move>& moves, Color us) const;
    void generateBishopMoves(std::vector<Move>& moves, Color us) const;
    void generateRookMoves(std::vector<Move>& moves, Color us) const;
    void generateQueenMoves(std::vector<Move>& moves, Color us) const;
    void generateKingMoves(std::vector<Move>& moves, Color us) const;
    void generateCastlingMoves(std::vector<Move>& moves, Color us) const;
    void addPromotions(std::vector<Move>& moves, Square from, Square to) const;
};