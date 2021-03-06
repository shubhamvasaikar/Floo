main: server.o client.o

client.o: ./client/client.cpp ./packet.h 
	g++ -g -pthread -std=c++0x ./client/client.cpp -o ./client/client.o

server.o: ./server/server.cpp ./packet.h 
	g++ -g -pthread -std=c++0x ./server/server.cpp -o ./server/server.o

clean:
	rm ./client/client.o ./client/*.txt ./server/server.o
