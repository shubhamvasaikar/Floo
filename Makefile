main: server.o client.o

client.o: ./client/client.c ./packet.h 
	gcc -g -pthread ./client/client.c -o ./client/client.o

server.o: ./server/server.c ./packet.h 
	gcc -g -pthread ./server/server.c -o ./server/server.o

clean:
	rm ./client/client.o ./client/*.txt ./server/server.o
