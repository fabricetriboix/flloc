
CC := clang
# XXX CFLAGS := -Wall -O2 -pthread
CFLAGS := -Wall -O0 -g -pthread
AR := ar
ARFLAGS := crs

all: clean libflloc.a unit-test

clean:
	rm -f *.a *.o expected-*.txt test.txt unit-test

libflloc.a: flloc.o
	$(AR) $(ARFLAGS) $@ $^

flloc.o: flloc.c
	$(CC) $(CFLAGS) -c $< -o $@

unit-test.o: unit-test.c
	$(CC) $(CFLAGS) -c $< -o $@

unit-test: unit-test.o libflloc.a
	$(CC) $(CFLAGS) -o $@ $^
