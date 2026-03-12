
CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2
LDFLAGS = -pthread
LIBS = -lws2_32

COMMON_SRC = common/Bitboard.cpp common/Board.cpp
COMMON_OBJ = Bitboard.o Board.o

SERVER_SRC = src/Server.cpp
CLIENT_SRC = src/Client.cpp

.PHONY: all clean

all: chess-server chess-client

chess-server: $(COMMON_OBJ) $(SERVER_SRC)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -Iinclude -o $@ $(COMMON_OBJ) $(SERVER_SRC) $(LIBS)

chess-client: $(COMMON_OBJ) $(CLIENT_SRC)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -Iinclude -o $@ $(COMMON_OBJ) $(CLIENT_SRC) $(LIBS)

Bitboard.o: common/Bitboard.cpp
	$(CXX) $(CXXFLAGS) -Iinclude -c -o $@ $<

Board.o: common/Board.cpp
	$(CXX) $(CXXFLAGS) -Iinclude -c -o $@ $<

clean:
	rm -f chess-server chess-client *.o
