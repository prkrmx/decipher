CC = gcc
SWIG = swig3.0

CFLAGS = -Wall -lm -std=gnu99 -O3 -lpthread -g -ggdb3

all: delta libvankus

.PHONY: clean d-swig d-wrap d-so delta libvankus v-swig v-wrap v-so

delta: d-swig d-so d-wrap

d-swig: 
	$(SWIG) -python delta.i

d-wrap: d-swig
	 $(CC) $(CFLAGS) -c -fpic delta.c delta_wrap.c -I/usr/include/python3.5m

d-so: d-wrap
	$(CC) -shared delta.o delta_wrap.o -o _delta.so

libvankus: v-swig v-wrap v-so

v-swig: 
	$(SWIG) -python libvankus.i

v-wrap: v-swig
	 $(CC) $(CFLAGS) -c -fpic libvankus.c libvankus_wrap.c -I/usr/include/python3.5m

v-so: v-wrap
	$(CC) -shared libvankus.o libvankus_wrap.o -o _libvankus.so


clean:
	rm -rf _delta.so delta_wrap.o delta.o _libvankus.so libvankus_wrap.o libvankus.o


