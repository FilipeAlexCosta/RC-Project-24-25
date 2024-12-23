CC=g++
FLAGS=-Wextra -Wall -std=c++17

all: app_client app_server

app_client: client/client.cpp common/common.cpp common/except.cpp
	$(CC) $(FLAGS) client/client.cpp common/common.cpp common/except.cpp -o app_client

app_server: server/server.cpp server/game.cpp common/common.cpp common/except.cpp
	$(CC) $(FLAGS) server/server.cpp server/game.cpp common/common.cpp common/except.cpp -o app_server 

clean:
	rm app_client app_server 

tejo:
	./app_client -n tejo.tecnico.ulisboa.pt -p 58011
