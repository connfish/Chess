#include <iostream>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <memory>
#include <atomic>
#include <cstring>
#include <csignal>

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
// Game Room: pairs two players and manages their game
// ============================================================
struct GameRoom {
    int playerFd[2] = {-1, -1};          // socket fds for white(0) and black(1)
    Color playerColor[2] = {WHITE, BLACK};
    GameState game;
    std::mutex mtx;
    std::atomic<bool> gameOver{false};
    std::atomic<bool> drawOffered{false};
    Color drawOfferingColor;

    GameRoom(int64_t timeMs, int64_t incMs) : game(timeMs, incMs) {}

    int opponentIdx(int idx) { return 1 - idx; }

    void broadcast(const std::string& msg) {
        for (int i = 0; i < 2; i++)
            if (playerFd[i] >= 0) Protocol::sendMsg(playerFd[i], msg);
    }

    void sendTimeUpdate() {
        std::string msg = "TIME " + std::to_string(game.currentTimeMs(WHITE)) +
                          " " + std::to_string(game.currentTimeMs(BLACK));
        broadcast(msg);
    }

    void sendBoard() {
        broadcast("BOARD " + game.board.toFEN());
    }

    void endGame(const std::string& reason) {
        gameOver = true;
        broadcast("GAME_OVER " + reason);
    }
};

// ============================================================
// Server state
// ============================================================
static int listenFd = -1;
static std::atomic<bool> serverRunning{true};
static std::mutex waitMtx;
static int waitingPlayer = -1;         // fd of player waiting for match
static int64_t defaultTimeMs = 60000;  // 1 minute bullet
static int64_t defaultIncMs = 0;       // no increment

static std::mutex roomsMtx;
static std::vector<std::shared_ptr<GameRoom>> activeRooms;

void signalHandler(int) {
    serverRunning = false;
    // Notify all active games that the server is shutting down
    std::lock_guard<std::mutex> lock(roomsMtx);
    for (auto& room : activeRooms) {
        if (!room->gameOver) {
            room->gameOver = true;
            room->broadcast("GAME_OVER Server shutting down");
        }
    }
    if (listenFd >= 0) close(listenFd);
}

// ============================================================
// Timer thread: checks for time-outs during a game
// ============================================================
void timerThread(std::shared_ptr<GameRoom> room) {
    while (!room->gameOver) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::lock_guard<std::mutex> lock(room->mtx);
        if (room->game.result != IN_PROGRESS) continue;

        Color toMove = room->game.board.sideToMove;
        int64_t t = room->game.currentTimeMs(toMove);
        if (t <= 0) {
            room->game.result = (toMove == WHITE) ? BLACK_WINS_TIMEOUT : WHITE_WINS_TIMEOUT;
            room->endGame(room->game.resultString());
        }
        room->sendTimeUpdate();
    }
}

// ============================================================
// Player handler thread
// ============================================================
void playerThread(std::shared_ptr<GameRoom> room, int playerIdx) {
    int fd = room->playerFd[playerIdx];
    Color myColor = room->playerColor[playerIdx];
    Protocol::LineReader reader(fd);

    while (!room->gameOver) {
        std::string line = reader.readLine();
        if (line.empty()) {
            // Disconnected
            std::lock_guard<std::mutex> lock(room->mtx);
            if (!room->gameOver) {
                room->game.result = (myColor == WHITE)
                    ? BLACK_WINS_RESIGNATION : WHITE_WINS_RESIGNATION;
                room->endGame(room->game.resultString() + " (disconnect)");
            }
            break;
        }

        std::string cmd = Protocol::getCommand(line);
        std::string args = Protocol::getArgs(line);

        std::lock_guard<std::mutex> lock(room->mtx);
        if (room->game.result != IN_PROGRESS) break;

        if (cmd == "MOVE") {
            // Validate it's this player's turn
            if (room->game.board.sideToMove != myColor) {
                Protocol::sendMsg(fd, "ERROR Not your turn");
                continue;
            }
            if (!room->game.tryMove(args)) {
                Protocol::sendMsg(fd, "ERROR Illegal move: " + args);
                continue;
            }

            // Start clock on first move
            if (!room->game.clockRunning && room->game.moveHistory.size() == 1) {
                room->game.startClock();
            }

            // Notify opponent
            int oppIdx = room->opponentIdx(playerIdx);
            Protocol::sendMsg(room->playerFd[oppIdx], "OPPONENT_MOVE " + args);
            room->sendBoard();
            room->sendTimeUpdate();

            if (room->game.result != IN_PROGRESS) {
                room->endGame(room->game.resultString());
            } else {
                Protocol::sendMsg(room->playerFd[oppIdx], "YOUR_TURN");
            }

        } else if (cmd == "RESIGN") {
            room->game.resign(myColor);
            room->endGame(room->game.resultString());

        } else if (cmd == "DRAW_OFFER") {
            room->drawOffered = true;
            room->drawOfferingColor = myColor;
            int oppIdx = room->opponentIdx(playerIdx);
            Protocol::sendMsg(room->playerFd[oppIdx], "DRAW_OFFER");

        } else if (cmd == "DRAW_ACCEPT") {
            if (room->drawOffered && room->drawOfferingColor != myColor) {
                room->game.result = DRAW_AGREEMENT;
                room->endGame(room->game.resultString());
            }

        } else if (cmd == "DRAW_DECLINE") {
            room->drawOffered = false;
            int oppIdx = room->opponentIdx(playerIdx);
            Protocol::sendMsg(room->playerFd[oppIdx], "DRAW_DECLINE");

        } else if (cmd == "CHAT") {
            int oppIdx = room->opponentIdx(playerIdx);
            Protocol::sendMsg(room->playerFd[oppIdx], "CHAT " + args);

        } else {
            Protocol::sendMsg(fd, "ERROR Unknown command: " + cmd);
        }
    }

    close(fd);
}

// ============================================================
// Match-making: pair waiting players
// ============================================================
void startGame(int fd1, int fd2) {
    auto room = std::make_shared<GameRoom>(defaultTimeMs, defaultIncMs);
    room->playerFd[0] = fd1; // White
    room->playerFd[1] = fd2; // Black

    // Send welcome
    Protocol::sendMsg(fd1, "WELCOME WHITE " + std::to_string(defaultTimeMs) +
                           " " + std::to_string(defaultIncMs));
    Protocol::sendMsg(fd2, "WELCOME BLACK " + std::to_string(defaultTimeMs) +
                           " " + std::to_string(defaultIncMs));

    // Send initial board
    room->sendBoard();
    room->sendTimeUpdate();
    Protocol::sendMsg(fd1, "YOUR_TURN");

    // Register room
    {
        std::lock_guard<std::mutex> lock(roomsMtx);
        activeRooms.push_back(room);
    }

    // Launch threads
    std::thread t1(playerThread, room, 0);
    std::thread t2(playerThread, room, 1);
    std::thread tt(timerThread, room);

    t1.detach();
    t2.detach();
    tt.detach();
}

// ============================================================
// Main
// ============================================================
int main(int argc, char* argv[]) {
    BB::initBitboards();

    int port = 5555;
    if (argc > 1) port = std::atoi(argv[1]);
    if (argc > 2) defaultTimeMs = std::atol(argv[2]) * 1000; // seconds -> ms
    if (argc > 3) defaultIncMs = std::atol(argv[3]) * 1000;

    std::signal(SIGINT, signalHandler);
#ifndef _WIN32
    std::signal(SIGPIPE, SIG_IGN);
#endif

#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd < 0) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(listenFd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind"); return 1;
    }
    if (listen(listenFd, 10) < 0) {
        perror("listen"); return 1;
    }

    std::cout << "=== Terminal Chess Server ===" << std::endl;
    std::cout << "Listening on port " << port << std::endl;
    std::cout << "Time control: " << defaultTimeMs / 1000 << "s + "
              << defaultIncMs / 1000 << "s" << std::endl;
    std::cout << "Waiting for players..." << std::endl;

    while (serverRunning) {
        sockaddr_in clientAddr{};
        socklen_t len = sizeof(clientAddr);
        int clientFd = accept(listenFd, (sockaddr*)&clientAddr, &len);
        if (clientFd < 0) {
            if (serverRunning) perror("accept");
            break;
        }

        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, ip, sizeof(ip));
        std::cout << "Player connected from " << ip << ":"
                  << ntohs(clientAddr.sin_port) << std::endl;

        std::lock_guard<std::mutex> lock(waitMtx);
        if (waitingPlayer < 0) {
            waitingPlayer = clientFd;
            Protocol::sendMsg(clientFd, "WAITING");
            std::cout << "Player queued, waiting for opponent..." << std::endl;
        } else {
            std::cout << "Match found! Starting game." << std::endl;
            startGame(waitingPlayer, clientFd);
            waitingPlayer = -1;
        }
    }

    close(listenFd);
#ifdef _WIN32
    WSACleanup();
#endif
    std::cout << "\nServer shut down." << std::endl;
    return 0;
}