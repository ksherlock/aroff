
CFLAGS += -g -Wall
LDFLAGS += -ltermcap

aroff: aroff.o aw.o awgs.o
aroff.o : aroff.c aroff.h
aw.o : aw.c aroff.h
awgs.o : awgs.c aroff.h