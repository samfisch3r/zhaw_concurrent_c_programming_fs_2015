CC  = gcc
#CFLAGS = -Wall -Wpedantic -Wextra
APP = client
SERV = server

all: client server

clean:
	rm -f $(APP) $(SERV)

client:
	$(CC) $(APP).c $(CFLAGS) -o $(APP)

server:
	$(CC) $(SERV).c $(CFLAGS) -pthread -o $(SERV)