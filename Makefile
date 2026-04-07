CC = gcc
CFLAGS = -Wall -O2 -fPIC -pthread
LDFLAGS = -ldl -lpthread -lrt
TARGET = sign_mt

all: $(TARGET) libsymbols.so

$(TARGET): server.o thread_pool.o
	$(CC) $(CFLAGS) server.o thread_pool.o -o $(TARGET) $(LDFLAGS)

libsymbols.so: symbols.c
	$(CC) -shared -fPIC -o libsymbols.so symbols.c

clean:
	rm -f *.o $(TARGET) libsymbols.so

.PHONY: all clean
