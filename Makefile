CC=g++
FLAGS=-g

all: client server

client: client.cpp common.cpp
	$(CC) $(FLAGS) client.cpp common.cpp -o client

server: server.cpp common.cpp
	$(CC) $(FLAGS) server.cpp common.cpp -o server 

clean:
	rm -f client server