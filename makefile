CC = gcc
FLAGS = -Wall -Wpedantic -Wextra -Werror

fat32: fat32.c
	$(CC) -g -o fat32 fat32.c $(FALGS)