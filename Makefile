CC=cc
CFLAGS=-g -Wall -Werror -std=c99
LFLAGS= -lm
TARGETS=meta_stats_server

all:  $(TARGETS)

$(TARGETS): % : %.o
	$(CC) $(CFLAGS) -o $@ $< $(LFLAGS)

clean:
	rm -rf *.o a.out $(TARGETS)
