main: server client

client: ./client/client.c ./packet.h ./client/client.o
	gcc -g ./client/client.c -o ./client/client.o

server: ./server/server.c ./packet.h ./server/server.o
	gcc -g ./server/server.c -o ./server/server.o

clean:
	rm ./client/client.o ./client/*.txt ./server/server.o
