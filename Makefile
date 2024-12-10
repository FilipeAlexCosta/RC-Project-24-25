CC=g++
FLAGS=-g

all: app_client app_server

app_client: client/client.cpp common/common.cpp
	$(CC) $(FLAGS) client/client.cpp common/common.cpp -o app_client

app_server: server/server.cpp server/game.cpp common/common.cpp
	$(CC) $(FLAGS) server/server.cpp server/game.cpp server/scoreboard.cpp common/common.cpp -o app_server 

clean:
	rm app_client app_server 

tejo:
	./app_client -n tejo.tecnico.ulisboa.pt -p 58011
