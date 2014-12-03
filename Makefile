all:
	gcc server.c -o server -lpthread -lmysqlclient
	gcc client.c -o client -lpthread -lcurses

clean:
	rm -f server client