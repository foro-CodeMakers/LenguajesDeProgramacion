CC=gcc
LD=gcc
CFLAGS=-Wall -g `pkg-config --cflags libavcodec libswscale`
LDFLAGS=`pkg-config --libs libavcodec libswscale libavutil`

.PHONY: clean all

all: camara

camara: camara.o
	$(LD) $? -o $@ $(LDFLAGS)

clean:
	- rm camara.o camara *~

%.o: %.c
	$(CC) -c $< -o $@ $(CFLAGS)

