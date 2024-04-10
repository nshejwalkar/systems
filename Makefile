mysh: mysh.c
	$(CC) $(CFLAGS) $^ -o $@

CC = gcc
CFLAGS = -g -Wall -fsanitize=address,undefined