CC=clang
CFLAGS+=-Wall $(shell pkg-config --cflags gtk+-2.0) \
	$(shell pkg-config --cflags indicator)
LIBS=$(shell pkg-config --libs gtk+-2.0) \
	$(shell pkg-config --libs indicator)

all:    panel

panel:  panel.o tray.o
	$(CC) $(LDFLAGS) $(LIBS) -o $@ $^

%.o:	%.c
	$(CC) $(CFLAGS) -c -o $@ $^

