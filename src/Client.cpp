#include <iostream>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <sstream>
#include <cstring>
#include <csignal>
#include <algorithm>
#include <chrono>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #define close closesocket
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
#endif

#include "Board.hpp"
#include "BitBoard.hpp"
#include "Game.hpp"
#include "Protocol.hpp"

// ============================================================
// ANSI terminal helpers
// ============================================================
namespace Term {
    const char* RESET   = "\033[0m";
    const char* BOLD    = "\033[1m";
    const char* DIM     = "\033[2m";
    const char* RED     = "\033[31m";
    const char* GREEN   = "\033[32m";
    const char* YELLOW  = "\033[33m";
    const char* BLUE    = "\033[34m";
    const char* MAGENTA = "\033[35m";
    const char* CYAN    = "\033[36m";
    const char* WHITE   = "\033[37m";
    const char* BG_DARK  = "\033[48;5;95m";   // dark square
    const char* BG_LIGHT = "\033[48;5;222m";  // light square
    const char* FG_WHITE_PIECE = "\033[97m";   // bright white
    const char* FG_BLACK_PIECE = "\033[30m";   // black

    void clearScreen() { std::cout << "\033[H" << std::flush; }
    void moveCursor(int row, int col) { std::cout << "\033[" << row << ";" << col << "H" << std::flush; }
}

// ============================================================
// Client state
// ============================================================
static Board localBoard;
static Color myColor = ::WHITE;
static bool myTurn = false;
static std::atomic<bool> connected{true};
static std::atomic<bool> gameActive{false};
static std::mutex displayMtx;
static int64_t whiteTimeMs = 0, blackTimeMs = 0;
static std::string lastStatus;
static std::vector<std::string> moveLog;

// Local clock countdown
static std::atomic<bool> clientClockRunning{false};
static Color activeSide = ::WHITE;
static std::chrono::steady_clock::time_point lastTimeUpdate;

static int globalSockfd = -1;

void clientSignalHandler(int) {
    if (gameActive && globalSockfd >= 0) {
        Protocol::sendMsg(globalSockfd, "RESIGN");
    }
    connected = false;
}

// Unicode chess pieces
const char* unicodePieces[2][7] = {
    {"  ", " \u2659", " \u2658", " \u2657", " \u2656", " \u2655", " \u2654"},  // White
    {"  ", " \u265F", " \u265E", " \u265D", " \u265C", " \u265B", " \u265A"}   // Black
};

// Fallback ASCII pieces
const char asciiPieces[2][7] = {
    {' ', 'P', 'N', 'B', 'R', 'Q', 'K'},
    {' ', 'p', 'n', 'b', 'r', 'q', 'k'}
};

static bool useUnicode = false;

// ============================================================
// Board rendering
// ============================================================
void renderBoard() {
    std::lock_guard<std::mutex> lock(displayMtx);
    Term::clearScreen();

    bool flipped = (myColor == BLACK);

    // Title
    std::cout << Term::BOLD << Term::CYAN
              << "  +===================================+\n"
              << "  |        TERMINAL BULLET CHESS       |\n"
              << "  +===================================+"
              << Term::RESET << "\n\n";

    // Compute locally adjusted times
    int64_t wTime = whiteTimeMs;
    int64_t bTime = blackTimeMs;
    if (clientClockRunning) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - lastTimeUpdate).count();
        if (activeSide == ::WHITE) wTime = std::max(0LL, wTime - elapsed);
        else                       bTime = std::max(0LL, bTime - elapsed);
    }

    // Top player info (opponent)
    Color topColor = flipped ? ::WHITE : BLACK;
    std::string topName = (topColor == ::WHITE) ? "White" : "Black";
    int64_t topTime = (topColor == ::WHITE) ? wTime : bTime;
    std::cout << "  " << Term::BOLD << topName << Term::RESET
              << "  " << Term::YELLOW << GameState::formatTime(topTime) << Term::RESET << "\n\n";

    // Board
    for (int displayRank = 0; displayRank < 8; displayRank++) {
        int r = flipped ? displayRank : (7 - displayRank);
        std::cout << " " << Term::DIM << (r + 1) << Term::RESET << " ";

        for (int displayFile = 0; displayFile < 8; displayFile++) {
            int f = flipped ? (7 - displayFile) : displayFile;
            Square s = makeSquare(f, r);
            bool lightSquare = ((f + r) % 2 != 0);

            std::cout << (lightSquare ? Term::BG_LIGHT : Term::BG_DARK);

            if (localBoard.pieceOn[s] != NO_PIECE) {
                Color pc = localBoard.colorOn[s];
                std::cout << (pc == ::WHITE ? Term::FG_WHITE_PIECE : Term::FG_BLACK_PIECE);
                if (useUnicode)
                    std::cout << unicodePieces[pc][localBoard.pieceOn[s]];
                else
                    std::cout << " " << asciiPieces[pc][localBoard.pieceOn[s]];
                std::cout << " ";
            } else {
                std::cout << "   ";
            }
            std::cout << Term::RESET;
        }

        // Move log on the right
        int moveIdx = displayRank;
        if (moveIdx < (int)moveLog.size()) {
            std::cout << "   " << Term::DIM << moveLog[moveIdx] << Term::RESET;
        }
        std::cout << "\n";
    }

    // File labels
    std::cout << "   ";
    for (int displayFile = 0; displayFile < 8; displayFile++) {
        int f = flipped ? (7 - displayFile) : displayFile;
        std::cout << " " << Term::DIM << char('a' + f) << Term::RESET << " ";
    }
    std::cout << "\n\n";

    // Bottom player info (you)
    Color botColor = flipped ? BLACK : ::WHITE;
    std::string botName = (botColor == ::WHITE) ? "White" : "Black";
    int64_t botTime = (botColor == ::WHITE) ? wTime : bTime;
    std::cout << "  " << Term::BOLD << botName << " (you)"
              << Term::RESET << "  " << Term::YELLOW
              << GameState::formatTime(botTime) << Term::RESET << "\n\n";

    // Status line
    if (!lastStatus.empty()) {
        std::cout << "  " << Term::MAGENTA << lastStatus << Term::RESET << "\n";
    }

    // Turn indicator
    if (gameActive) {
        if (myTurn) {
            std::cout << "  " << Term::GREEN << Term::BOLD
                      << ">>> Your move: " << Term::RESET;
        } else {
            std::cout << "  " << Term::DIM << "Waiting for opponent..." << Term::RESET << "\n";
        }
    }

    std::cout << std::flush;
}

// ============================================================
// Parse FEN and update local board
// ============================================================
void parseFEN(const std::string& fen) {
    localBoard.clear();
    std::istringstream ss(fen);
    std::string placement, side, castling, ep;
    int hm, fm;
    ss >> placement >> side >> castling >> ep >> hm >> fm;

    int f = 0, r = 7;
    for (char c : placement) {
        if (c == '/') { r--; f = 0; }
        else if (c >= '1' && c <= '8') { f += c - '0'; }
        else {
            Color col = (c >= 'A' && c <= 'Z') ? ::WHITE : BLACK;
            char upper = (col == BLACK) ? (c - 'a' + 'A') : c;
            PieceType pt = NO_PIECE;
            switch (upper) {
                case 'P': pt = PAWN; break;   case 'N': pt = KNIGHT; break;
                case 'B': pt = BISHOP; break; case 'R': pt = ROOK; break;
                case 'Q': pt = QUEEN; break;  case 'K': pt = KING; break;
            }
            if (pt != NO_PIECE) localBoard.putPiece(col, pt, makeSquare(f, r));
            f++;
        }
    }
    localBoard.sideToMove = (side == "w") ? ::WHITE : BLACK;
}

// ============================================================
// Timer display thread: redraws board every 100ms for live countdown
// ============================================================
void timerDisplayThread() {
    while (connected) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (gameActive && clientClockRunning) {
            renderBoard();
        }
    }
}

// ============================================================
// Receiver thread: listens for server messages
// ============================================================
void receiverThread(int sockfd) {
    Protocol::LineReader reader(sockfd);

    while (connected) {
        std::string line = reader.readLine();
        if (line.empty()) {
            connected = false;
            lastStatus = "Disconnected from server.";
            renderBoard();
            break;
        }

        std::string cmd = Protocol::getCommand(line);
        std::string args = Protocol::getArgs(line);

        if (cmd == "WELCOME") {
            std::istringstream ss(args);
            std::string color;
            int64_t timeMs, incMs;
            ss >> color >> timeMs >> incMs;
            myColor = (color == "WHITE") ? ::WHITE : BLACK;
            whiteTimeMs = timeMs;
            blackTimeMs = timeMs;
            gameActive = true;
            lastStatus = "Game started! You are " + color + ".";
            renderBoard();

        } else if (cmd == "WAITING") {
            lastStatus = "Waiting for an opponent to connect...";
            renderBoard();

        } else if (cmd == "BOARD") {
            parseFEN(args);
            renderBoard();

        } else if (cmd == "YOUR_TURN") {
            myTurn = true;
            activeSide = myColor;
            lastTimeUpdate = std::chrono::steady_clock::now();
            clientClockRunning = true;
            renderBoard();

        } else if (cmd == "OPPONENT_MOVE") {
            std::string moveStr = args;
            myTurn = false;
            activeSide = (myColor == ::WHITE) ? BLACK : ::WHITE;
            lastTimeUpdate = std::chrono::steady_clock::now();
            clientClockRunning = true;
            // Add to move log
            int moveNum = (moveLog.size() / 2) + 1;
            if (moveLog.size() % 2 == 0)
                moveLog.push_back(std::to_string(moveNum) + ". " + moveStr);
            else
                moveLog.back() += "  " + moveStr;
            lastStatus = "Opponent played: " + moveStr;

        } else if (cmd == "TIME") {
            std::istringstream ss(args);
            ss >> whiteTimeMs >> blackTimeMs;
            lastTimeUpdate = std::chrono::steady_clock::now();

        } else if (cmd == "GAME_OVER") {
            clientClockRunning = false;
            gameActive = false;
            myTurn = false;
            lastStatus = args;
            renderBoard();
            std::cout << "\n  " << Term::BOLD << Term::RED << args << Term::RESET << "\n";
            std::cout << "  Press Enter to exit.\n";

        } else if (cmd == "ERROR") {
            lastStatus = "Error: " + args;
            renderBoard();

        } else if (cmd == "DRAW_OFFER") {
            lastStatus = "Opponent offers a draw. Type 'draw accept' or 'draw decline'.";
            renderBoard();

        } else if (cmd == "DRAW_DECLINE") {
            lastStatus = "Draw offer declined.";
            renderBoard();

        } else if (cmd == "CHAT") {
            lastStatus = "Opponent: " + args;
            renderBoard();
        }
    }
}

// ============================================================
// Print help
// ============================================================
void printHelp() {
    std::cout << "\n  " << Term::BOLD << "Commands:" << Term::RESET << "\n"
              << "  " << Term::CYAN << "e2e4" << Term::RESET << "      - Move (algebraic from-to)\n"
              << "  " << Term::CYAN << "e7e8q" << Term::RESET << "     - Promote to queen\n"
              << "  " << Term::CYAN << "resign" << Term::RESET << "    - Resign the game\n"
              << "  " << Term::CYAN << "draw" << Term::RESET << "      - Offer a draw\n"
              << "  " << Term::CYAN << "chat <msg>" << Term::RESET << " - Send chat message\n"
              << "  " << Term::CYAN << "board" << Term::RESET << "     - Redraw the board\n"
              << "  " << Term::CYAN << "ascii" << Term::RESET << "     - Toggle Unicode/ASCII pieces\n"
              << "  " << Term::CYAN << "help" << Term::RESET << "      - Show this help\n"
              << "  " << Term::CYAN << "quit" << Term::RESET << "      - Quit\n\n";
}

// ============================================================
// Main
// ============================================================
int main(int argc, char* argv[]) {
    BB::initBitboards();
    localBoard.setStartPos();

    std::string host = "127.0.0.1";
    int port = 5555;

    if (argc > 1) host = argv[1];
    if (argc > 2) port = std::atoi(argv[2]);

    std::signal(SIGINT, clientSignalHandler);
#ifndef _WIN32
    std::signal(SIGPIPE, SIG_IGN);
#endif

#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    // Connect to server
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    globalSockfd = sockfd;
    if (sockfd < 0) { perror("socket"); return 1; }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);

    if (inet_pton(AF_INET, host.c_str(), &serverAddr.sin_addr) <= 0) {
        std::cerr << "Invalid address: " << host << std::endl;
        return 1;
    }

    std::cout << Term::BOLD << Term::CYAN
              << "\n  Terminal Bullet Chess Client\n" << Term::RESET
              << "  Connecting to " << host << ":" << port << "...\n";

    if (connect(sockfd, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        perror("connect");
        std::cerr << "  Could not connect to server. Is the server running?\n";
        return 1;
    }

    std::cout << Term::GREEN << "  Connected!\n" << Term::RESET;

    // Start receiver thread
    std::thread recvThread(receiverThread, sockfd);
    recvThread.detach();

    // Start timer display thread
    std::thread timerThread(timerDisplayThread);
    timerThread.detach();

    // Input loop
    std::string input;
    while (connected) {
        if (!std::getline(std::cin, input)) break;

        // Trim whitespace
        while (!input.empty() && (input.front() == ' ' || input.front() == '\t')) input.erase(0, 1);
        while (!input.empty() && (input.back() == ' ' || input.back() == '\t')) input.pop_back();
        std::string lower = input;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

        if (lower.empty()) {
            if (myTurn) renderBoard();
            continue;
        }

        if (lower == "quit" || lower == "exit") {
            if (gameActive) Protocol::sendMsg(sockfd, "RESIGN");
            break;
        }

        if (lower == "help" || lower == "?") {
            printHelp();
            continue;
        }

        if (lower == "board") {
            renderBoard();
            continue;
        }

        if (lower == "ascii") {
            useUnicode = !useUnicode;
            renderBoard();
            continue;
        }

        if (lower == "resign") {
            Protocol::sendMsg(sockfd, "RESIGN");
            continue;
        }

        if (lower == "draw") {
            Protocol::sendMsg(sockfd, "DRAW_OFFER");
            lastStatus = "Draw offered to opponent.";
            renderBoard();
            continue;
        }

        if (lower == "draw accept") {
            Protocol::sendMsg(sockfd, "DRAW_ACCEPT");
            continue;
        }

        if (lower == "draw decline") {
            Protocol::sendMsg(sockfd, "DRAW_DECLINE");
            continue;
        }

        if (lower.substr(0, 4) == "chat") {
            std::string msg = (input.length() > 5) ? input.substr(5) : "";
            if (!msg.empty()) Protocol::sendMsg(sockfd, "CHAT " + msg);
            continue;
        }

        // Must be a move
        if (!gameActive) {
            std::cout << "  Game is not active.\n";
            continue;
        }
        if (!myTurn) {
            std::cout << "  Not your turn!\n";
            continue;
        }

        // Validate locally first
        Move m = Move::fromAlgebraic(lower);
        if (!m.isValid()) {
            lastStatus = "Invalid format. Use e.g. e2e4 or e7e8q";
            renderBoard();
            continue;
        }

        Move legal = localBoard.matchMove(m);
        if (!legal.isValid()) {
            lastStatus = "Illegal move: " + lower;
            renderBoard();
            continue;
        }

        // Send to server
        Protocol::sendMsg(sockfd, "MOVE " + lower);
        myTurn = false;

        // Update local move log
        int moveNum = (moveLog.size() / 2) + 1;
        if (moveLog.size() % 2 == 0)
            moveLog.push_back(std::to_string(moveNum) + ". " + lower);
        else
            moveLog.back() += "  " + lower;
    }

    connected = false;
    close(sockfd);
#ifdef _WIN32
    WSACleanup();
#endif
    std::cout << "\n  " << Term::DIM << "Goodbye!" << Term::RESET << "\n";
    return 0;
}