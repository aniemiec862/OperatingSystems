all: compile

compile:
	gcc -Wall server.c -o server
	gcc -Wall client.c -o client

test:
	@echo "[TEST]"
	@./server

clean:
	rm server
	rm client