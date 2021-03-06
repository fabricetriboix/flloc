
CC := gcc
CFLAGS := -Wall -Wno-pointer-to-int-cast -O2 -pthread
#CFLAGS := -Wall -Wno-pointer-to-int-cast -O0 -g -pthread
AR := ar
ARFLAGS := crs
PREFIX := /usr/local

all: clean libflloc.a unit-test

test: all
	./run-tests.py

clean:
	rm -f *.a *.o expected-*.txt test.txt unit-test mtrace.*

install: libflloc.a
	mkdir -p $(PREFIX)/lib; \
	mkdir -p $(PREFIX)/include; \
	cp -af $< $(PREFIX)/lib; \
	cp -af flloc.h $(PREFIX)/include

libflloc.a: flloc.o
	$(AR) $(ARFLAGS) $@ $^

flloc.o: flloc.c
	$(CC) $(CFLAGS) -c $< -o $@

unit-test: unit-test.c libflloc.a
	$(CC) $(CFLAGS) -o $@ $^
