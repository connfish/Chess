#pragma once
#include <string>
#include <cstring>
#include <sstream>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #ifndef MSG_NOSIGNAL
    #define MSG_NOSIGNAL 0
  #endif
#else
  #include <sys/socket.h>
#endif

/*
 * Wire protocol: Simple text-based messages terminated by newline '\n'
 *
 * Server -> Client messages:
 *   WELCOME <color> <time_ms> <inc_ms>         - Game start, assigned color
 *   BOARD <fen>                                 - Current board state
 *   OPPONENT_MOVE <move>                        - Opponent made a move
 *   YOUR_TURN                                   - It's your turn
 *   TIME <white_ms> <black_ms>                  - Clock update
 *   GAME_OVER <result_string>                   - Game ended
 *   ERROR <message>                             - Invalid move or error
 *   WAITING                                     - Waiting for opponent
 *   CHAT <message>                              - Chat from opponent
 *
 * Client -> Server messages:
 *   MOVE <algebraic>                            - e.g., MOVE e2e4
 *   RESIGN                                      - Resign the game
 *   DRAW_OFFER                                  - Offer a draw
 *   DRAW_ACCEPT                                 - Accept draw offer
 *   DRAW_DECLINE                                - Decline draw offer
 *   CHAT <message>                              - Send chat to opponent
 */

namespace Protocol {

const int MAX_MSG_LEN = 1024;

// Blocking send of a complete message (with newline)
inline bool sendMsg(int sockfd, const std::string& msg) {
    std::string full = msg + "\n";
    size_t total = 0;
    while (total < full.size()) {
        ssize_t sent = send(sockfd, full.c_str() + total, full.size() - total, MSG_NOSIGNAL);
        if (sent <= 0) return false;
        total += sent;
    }
    return true;
}

// Buffered line reader
class LineReader {
    int fd;
    std::string buffer;
public:
    LineReader(int fd) : fd(fd) {}

    // Returns next complete line, or empty string on disconnect
    std::string readLine() {
        while (true) {
            size_t pos = buffer.find('\n');
            if (pos != std::string::npos) {
                std::string line = buffer.substr(0, pos);
                buffer.erase(0, pos + 1);
                // Strip \r if present
                if (!line.empty() && line.back() == '\r') line.pop_back();
                return line;
            }
            char tmp[512];
            ssize_t n = recv(fd, tmp, sizeof(tmp), 0);
            if (n <= 0) return "";
            buffer.append(tmp, n);
        }
    }
};

inline std::string getCommand(const std::string& msg) {
    size_t sp = msg.find(' ');
    return (sp == std::string::npos) ? msg : msg.substr(0, sp);
}

inline std::string getArgs(const std::string& msg) {
    size_t sp = msg.find(' ');
    return (sp == std::string::npos) ? "" : msg.substr(sp + 1);
}

} // namespace Protocol