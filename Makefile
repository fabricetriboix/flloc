
CC := clang
CFLAGS := -Wall -O2 -pthread
AR := ar
ARFLAGS := crs

all: libflloc.a

clean:
	rm -f *.a *.o

libflloc.a: flloc.o
	$(AR) $(ARFLAGS) $@ $^

flloc.o: flloc.c
	$(CC) $(CFLAGS) -c $< -o $@
