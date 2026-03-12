# Terminal Bullet Chess

A two-player online bullet chess game played in the terminal, featuring a bitboard-based rules engine (inspired by [Deepov](https://github.com/RomainGoussault/Deepov)), TCP/IP socket communication, and a multithreaded server for real-time move validation and synchronization.

## Features

- **Bitboard chess engine** — Full legal move generation including castling, en passant, promotions, and all special rules
- **Rules engine** — Checkmate, stalemate, 50-move rule, insufficient material, and threefold repetition detection
- **TCP/IP networking** — Text-based wire protocol over TCP sockets for reliable communication
- **Multithreaded server** — Each game room runs player handler threads + a timer thread for concurrent move processing
- **Bullet chess clock** — Configurable time controls with per-move increment support; real-time countdown with 100ms resolution
- **Rich terminal UI** — ANSI color-coded board with Unicode chess pieces, live clock display, and move history sidebar
- **Match-making** — Server automatically pairs connecting players into games
- **In-game chat** — Players can send messages to each other during the game
- **Draw offers & resignation** — Full game management with draw offer/accept/decline protocol

## Architecture

```
terminal-chess/
├── include/
│   ├── Types.hpp       # Core types: Square, Move, PieceType, Color
│   ├── BitBoard.hpp    # Bitboard utilities, attack tables
│   ├── Board.hpp       # Board representation & move generation
│   ├── Game.hpp        # Game state, timers, result tracking
│   └── Protocol.hpp    # TCP wire protocol (text-based, newline-delimited)
├── common/
│   ├── BitBoard.cpp    # Attack table initialization, sliding piece rays
│   └── Board.cpp       # Full chess logic: move gen, validation, FEN
├── src/
│   ├── server.cpp      # Multithreaded TCP server with game rooms
│   └── client.cpp      # Terminal client with ANSI board rendering
└── Makefile
```

## Building

Requires a C++17 compiler. Supports Windows (MSYS2 UCRT64), Linux, and macOS.

**Windows (MSYS2):** Install MSYS2 from https://www.msys2.org, then open the MSYS2 UCRT64 terminal and run:

```bash
pacman -S mingw-w64-ucrt-x86_64-gcc make
```

```bash make ```

This produces two binaries: `chess-server` and `chess-client`.

## Running

### Start the server

```bash
# Default: port 5555, 60-second bullet, no increment
./chess-server

# Custom: port 8080, 120-second time control, 1-second increment
./chess-server 8080 120 1
```

**Server arguments:** `./chess-server [port] [time_seconds] [increment_seconds]`

### Connect clients

Open two separate terminals and run:

```bash
# Player 1 (local)
./chess-client

# Player 2 (local or remote)
./chess-client 127.0.0.1 5555

# Connect to a remote server
./chess-client <server-ip> 5555
```

**Client arguments:** `./chess-client [server_ip] [port]`

### Playing

The first player to connect gets White. Moves use long algebraic notation:

| Command          | Description                        |
|------------------|------------------------------------|
| `e2e4`           | Move pawn from e2 to e4            |
| `e7e8q`          | Promote to queen                   |
| `e1g1`           | Castle kingside (move king)        |
| `resign`         | Resign the game                    |
| `draw`           | Offer a draw                       |
| `draw accept`    | Accept opponent's draw offer       |
| `chat hello!`    | Send a chat message                |
| `board`          | Redraw the board                   |
| `ascii`          | Toggle Unicode/ASCII piece display |
| `help`           | Show all commands                  |
| `quit`           | Exit (auto-resigns if in game)     |

## Playing Across Two Laptops (LAN)

Both laptops must be on the same Wi-Fi network.

### Step 1 — Find the server laptop's IP

On the server laptop, open a terminal and run:

```bash
ipconfig
```

Look for the **IPv4 Address** under your Wi-Fi adapter (e.g. `192.168.x.x`).

### Step 2 — Allow the port through Windows Firewall

On the server laptop, run this in PowerShell **as Administrator**:

```powershell
New-NetFirewallRule -DisplayName "Chess Server" -Direction Inbound -Protocol TCP -LocalPort 5555 -Action Allow
```

### Step 3 — Start the server

On the server laptop:

```bash
./chess-server
```

### Step 4 — Get the client on the other laptop

Copy `chess-client.exe` to the other laptop. It also needs these DLLs from `C:\msys64\ucrt64\bin\` placed in the same folder:

- `libstdc++-6.dll`
- `libwinpthread-1.dll`
- `libgcc_s_seh-1.dll`

Alternatively, build with static linking so no DLLs are needed. In the makefile change:

```makefile
LDFLAGS = -pthread -static-libgcc -static-libstdc++ -Wl,-Bstatic -lpthread -Wl,-Bdynamic
```

Then rebuild with `make`.

### Step 5 — Connect from the other laptop

```bash
./chess-client <server-ip> 5555
```

Replace `<server-ip>` with the IP address from Step 1. Both players connect this way — one from each laptop.

## Wire Protocol

Simple text-based, newline-delimited messages over TCP:

**Server → Client:**
- `WELCOME WHITE 60000 0` — assigned color + time control
- `BOARD <fen>` — current position
- `OPPONENT_MOVE e7e5` — opponent's move
- `YOUR_TURN` — it's your move
- `TIME 58320 59100` — clock update (white_ms black_ms)
- `GAME_OVER Black wins by checkmate!`
- `ERROR Illegal move: e2e5`

**Client → Server:**
- `MOVE e2e4` — make a move
- `RESIGN` / `DRAW_OFFER` / `DRAW_ACCEPT`
- `CHAT gg wp`

## Chess Engine Design

The engine uses a **bitboard** representation inspired by [Deepov](https://github.com/RomainGoussault/Deepov):

- **64-bit integers** represent piece positions, one bitboard per piece type per color
- **Pre-computed attack tables** for knights, kings, and pawn captures
- **Classical ray attacks** for sliding pieces (bishops, rooks, queens)
- **Copy-make** approach for move legality testing — copies the board, applies the move, checks if own king is in check
- Full support for all special moves: castling rights tracking, en passant square, pawn promotion

## Extending

To integrate the full Deepov engine for AI opponents:

1. Clone Deepov into a `deepov/` subdirectory
2. Use `Board` state to initialize Deepov's board
3. Call Deepov's search to get best moves for an AI player
4. Add an AI mode to the server that replaces one player thread with an engine thread

## License

This project's chess engine component is inspired by Deepov (GPL-3.0). The networking and UI layers are original code.