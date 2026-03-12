#pragma once
#include "Board.hpp"
#include <chrono>
#include <vector>
#include <string>

enum GameResult {
    IN_PROGRESS,
    WHITE_WINS_CHECKMATE,
    BLACK_WINS_CHECKMATE,
    WHITE_WINS_TIMEOUT,
    BLACK_WINS_TIMEOUT,
    WHITE_WINS_RESIGNATION,
    BLACK_WINS_RESIGNATION,
    DRAW_STALEMATE,
    DRAW_50_MOVE,
    DRAW_INSUFFICIENT,
    DRAW_AGREEMENT,
    DRAW_THREEFOLD
};

struct GameState {
    Board board;
    GameResult result;

    // Bullet chess timers (in milliseconds)
    int64_t timeWhiteMs;
    int64_t timeBlackMs;
    int64_t incrementMs;  // per-move increment
    std::chrono::steady_clock::time_point lastMoveTime;
    bool clockRunning;

    // Move history
    std::vector<std::string> moveHistory;

    // Position history for threefold repetition
    std::vector<std::string> positionHistory;

    GameState(int64_t timeMs = 60000, int64_t incMs = 0)
        : result(IN_PROGRESS), timeWhiteMs(timeMs), timeBlackMs(timeMs),
          incrementMs(incMs), clockRunning(false) {
        board.setStartPos();
        positionHistory.push_back(board.toFEN().substr(0, board.toFEN().find(' ', board.toFEN().find(' ', board.toFEN().find(' ') + 1) + 1)));
    }

    void startClock() {
        lastMoveTime = std::chrono::steady_clock::now();
        clockRunning = true;
    }

    // Update the clock for the side that just moved, returns false if time ran out
    bool updateClock() {
        if (!clockRunning) return true;
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastMoveTime).count();

        // The side that just finished their move (opponent of current side to move)
        Color movedSide = ~board.sideToMove;
        int64_t& time = (movedSide == WHITE) ? timeWhiteMs : timeBlackMs;
        time -= elapsed;

        if (time <= 0) {
            time = 0;
            result = (movedSide == WHITE) ? BLACK_WINS_TIMEOUT : WHITE_WINS_TIMEOUT;
            return false;
        }

        time += incrementMs;
        lastMoveTime = now;
        return true;
    }

    int64_t currentTimeMs(Color c) const {
        int64_t t = (c == WHITE) ? timeWhiteMs : timeBlackMs;
        if (clockRunning && board.sideToMove == c) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastMoveTime).count();
            t -= elapsed;
            if (t < 0) t = 0;
        }
        return t;
    }

    bool tryMove(const std::string& moveStr) {
        if (result != IN_PROGRESS) return false;

        Move input = Move::fromAlgebraic(moveStr);
        if (!input.isValid()) return false;

        Move legal = board.matchMove(input);
        if (!legal.isValid()) return false;

        // Save state for unmake (not needed if we copy board)
        Board backup = board;

        if (!board.makeMove(legal)) {
            board = backup;
            return false;
        }

        // Update clock
        if (clockRunning) {
            if (!updateClock()) return true; // move was made but time ran out
        }

        moveHistory.push_back(legal.toAlgebraic());

        // Check for threefold repetition
        std::string posKey = board.toFEN();
        posKey = posKey.substr(0, posKey.find(' ', posKey.find(' ', posKey.find(' ') + 1) + 1));
        positionHistory.push_back(posKey);
        int count = 0;
        for (auto& p : positionHistory)
            if (p == posKey) count++;
        if (count >= 3) result = DRAW_THREEFOLD;

        // Check end conditions
        if (board.isCheckmate()) {
            result = (board.sideToMove == WHITE) ? BLACK_WINS_CHECKMATE : WHITE_WINS_CHECKMATE;
        } else if (board.isStalemate()) {
            result = DRAW_STALEMATE;
        } else if (board.isDraw()) {
            if (board.halfMoveClock >= 100) result = DRAW_50_MOVE;
            else result = DRAW_INSUFFICIENT;
        }

        return true;
    }

    void resign(Color c) {
        result = (c == WHITE) ? BLACK_WINS_RESIGNATION : WHITE_WINS_RESIGNATION;
    }

    std::string resultString() const {
        switch (result) {
            case WHITE_WINS_CHECKMATE:    return "White wins by checkmate!";
            case BLACK_WINS_CHECKMATE:    return "Black wins by checkmate!";
            case WHITE_WINS_TIMEOUT:      return "White wins on time!";
            case BLACK_WINS_TIMEOUT:      return "Black wins on time!";
            case WHITE_WINS_RESIGNATION:  return "White wins by resignation!";
            case BLACK_WINS_RESIGNATION:  return "Black wins by resignation!";
            case DRAW_STALEMATE:          return "Draw by stalemate.";
            case DRAW_50_MOVE:            return "Draw by 50-move rule.";
            case DRAW_INSUFFICIENT:       return "Draw by insufficient material.";
            case DRAW_AGREEMENT:          return "Draw by agreement.";
            case DRAW_THREEFOLD:          return "Draw by threefold repetition.";
            default:                      return "Game in progress.";
        }
    }

    static std::string formatTime(int64_t ms) {
        int sec = ms / 1000;
        int tenths = (ms % 1000) / 100;
        int min = sec / 60;
        sec %= 60;
        char buf[32];
        if (min > 0) snprintf(buf, sizeof(buf), "%d:%02d", min, sec);
        else snprintf(buf, sizeof(buf), "%d.%ds", sec, tenths);
        return buf;
    }
};