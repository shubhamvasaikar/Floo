main: server.o client.o

client.o: ./client/client.c ./packet.h 
	gcc -g ./client/client.c -o ./client/client.o

server.o: ./server/server.c ./packet.h 
	gcc -g ./server/server.c -o ./server/server.o

clean:
	rm ./client/client.o ./client/*.txt ./server/server.o
