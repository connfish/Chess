// client.c (engine-integrated)
// Compile: gcc -o client client.c chess_engine.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <arpa/inet.h>
#include <sys/select.h>

#include "chess_engine.h"

#define BUF_SIZE 512

int main(int argc, char **argv) {
    if (argc < 3) {
        printf("Usage: %s SERVER_IP PORT\n", argv[0]);
        return 1;
    }
    const char *server_ip = argv[1];
    int port = atoi(argv[2]);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); return 1; }
    struct sockaddr_in serv;
    serv.sin_family = AF_INET;
    serv.sin_port = htons(port);
    inet_pton(AF_INET, server_ip, &serv.sin_addr);

    if (connect(sock, (struct sockaddr*)&serv, sizeof(serv)) < 0) {
        perror("connect"); return 1;
    }

    printf("Connected to server %s:%d\n", server_ip, port);

    GameState gs;
    ge_init(&gs);
    ge_print(&gs);

    fd_set readfds;
    char buf[BUF_SIZE];
    bool my_turn = false;
    bool am_white = false;
    bool running = true;

    while (running) {
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        FD_SET(sock, &readfds);
        int maxfd = sock;

        int rv = select(maxfd+1, &readfds, NULL, NULL, NULL);
        if (rv < 0) { perror("select"); break; }

        if (FD_ISSET(sock, &readfds)) {
            ssize_t r = recv(sock, buf, sizeof(buf)-1, 0);
            if (r <= 0) {
                printf("Server disconnected.\n");
                break;
            }
            buf[r]=0;
            char *line = strtok(buf, "\n");
            while (line) {
                if (strncmp(line, "START WHITE", 11) == 0) {
                    am_white = true;
                    gs.white_to_move = true;
                    printf("You are WHITE. You move first.\n");
                } else if (strncmp(line, "START BLACK", 11) == 0) {
                    am_white = false;
                    gs.white_to_move = true; // server always uses turns; but our gs.white_to_move true means white side to move in orientation
                    printf("You are BLACK. Wait for white to move.\n");
                } else if (strncmp(line, "YOURTURN", 8) == 0) {
                    my_turn = true;
                    printf("\n--- YOUR TURN ---\n");
                } else if (strncmp(line, "MOVE ", 5) == 0) {
                    const char *mv = line + 5;
                    printf("Opponent played: %s\n", mv);
                    // apply opponent move using engine without legality check (we trust server), but if it fails try ge_try_move
                    if(!ge_apply_move_unchecked(&gs, mv)){
                        // fallback: try legal apply
                        if(!ge_try_move(&gs, mv)){
                            printf("Warning: failed to apply opponent move locally: %s\n", mv);
                        }
                    }
                    ge_print(&gs);
                } else if (strcmp(line, "OPPONENT_RESIGNED") == 0) {
                    printf("Opponent resigned. You win!\n");
                    running = false;
                } else if (strcmp(line, "OPPONENT_DISCONNECTED") == 0) {
                    printf("Opponent disconnected. Game over.\n");
                    running = false;
                } else {
                    printf("SERVER: %s\n", line);
                }
                line = strtok(NULL, "\n");
            }
        }

        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            char input[256];
            if (!fgets(input, sizeof(input), stdin)) { running = false; break; }
            input[strcspn(input, "\n")] = 0;
            if (strcmp(input, "") == 0) continue;

            if (strcmp(input, "quit") == 0) { running = false; break; }
            if (strcmp(input, "board") == 0) { ge_print(&gs); continue; }
            if (strcmp(input, "resign") == 0) {
                send(sock, "RESIGN\n", 7, 0);
                printf("You resigned.\n");
                running = false;
                break;
            }

            if (my_turn) {
                // user inputs e2e4 or e7e8q (promotion)
                if (strlen(input) >= 4) {
                    // validate and apply locally using ge_try_move
                    // But our ge_try_move uses g->white_to_move to know whose turn; ensure correct: gs.white_to_move should reflect real side to move.
                    // The server sends YOURTURN based on colors; so we don't need to map colors here. We will assume server ensures turns. We use gs.white_to_move to determine which side moves in engine.
                    // But gs.white_to_move is initialized to true; if you're black and server gave START BLACK, we did not flip it above; for simplicity we will maintain engine orientation as white on bottom (same as earlier),
                    // and treat moves text literally as algebraic coordinates independent of color. So we let engine enforce turn parity by checking piece color.
                    bool ok = ge_try_move(&gs, input);
                    if (!ok) {
                        printf("Illegal move (engine rejected). Try again.\n");
                    } else {
                        // send to server
                        char msg[BUF_SIZE];
                        snprintf(msg, sizeof(msg), "MOVE %s\n", input);
                        send(sock, msg, strlen(msg), 0);
                        ge_print(&gs);
                        my_turn = false;
                    }
                } else {
                    printf("Type a move like e2e4, or 'resign', 'board'.\n");
                }
            } else {
                printf("Not your turn. Type 'board' to see board or wait.\n");
            }
        }
    }

    close(sock);
    return 0;
}
