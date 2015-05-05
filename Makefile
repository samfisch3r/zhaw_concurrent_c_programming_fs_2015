CC  = gcc
APP = client
SERV = server

all: app serv

clean:
	rm -f $(APP) $(SERV)

app:
	$(CC) $(APP).c -Wall -Wpedantic -Wextra -o $(APP)

serv:
	$(CC) $(SERV).c -pthread -Wall -Wpedantic -Wextra -o $(SERV)

