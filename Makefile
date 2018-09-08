CC=gcc
CFLAGS=-std=c99 -pedantic-errors
SRC=cnvltool.c
EXEC=cnvltool

all:
	$(CC) $(SRC) -o $(EXEC) $(CFLAGS)
