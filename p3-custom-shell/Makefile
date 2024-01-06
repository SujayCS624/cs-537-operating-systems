CC = gcc
CFLAGS = -Wall -Werror -pedantic -std=gnu18
LOGIN = sujaycs
SUBMITPATH = ~cs537-1/handin/sujaycs/P3

all: wsh
.PHONY: all

wsh: wsh.c wsh.h
	$(CC) $(CFLAGS) $^ -o $@

run: wsh
	./wsh

pack: wsh.h wsh.c Makefile README.md
	tar -czvf $(LOGIN).tar.gz $^

submit: pack
	cp $(LOGIN).tar.gz $(SUBMITPATH)

