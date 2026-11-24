
CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -pthread
LDFLAGS = -lws2_32

SERVER = server.exe
CLIENT = client.exe

all: $(SERVER) $(CLIENT)

$(SERVER): server/server.cpp
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

$(CLIENT): client/client.cpp
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

server: $(SERVER)

client: $(CLIENT)

clean:
	rm -f $(SERVER) $(CLIENT)

.PHONY: all server client clean
