CXX      ?= g++
CC       ?= gcc
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
CFLAGS   ?= -std=c11 -O2 -Wall -Wextra
LIB      := ../lib

ZX0_SRCS = $(LIB)/zx0/compress.c $(LIB)/zx0/optimize.c $(LIB)/zx0/memory.c
ZX0_OBJS = $(patsubst $(LIB)/zx0/%.c,zx0_%.o,$(ZX0_SRCS))

mo5z: mo5z.cpp $(ZX0_OBJS)
	$(CXX) $(CXXFLAGS) -I$(LIB) -o $@ $< $(ZX0_OBJS)

test_mo5z: test_mo5z.cpp mo5z.cpp $(ZX0_OBJS) $(LIB)/catch2/catch_amalgamated.cpp
	$(CXX) $(CXXFLAGS) -I$(LIB) -DMO5Z_TESTING -o $@ test_mo5z.cpp $(LIB)/catch2/catch_amalgamated.cpp $(ZX0_OBJS)

zx0_%.o: $(LIB)/zx0/%.c $(LIB)/zx0/zx0.h
	$(CC) $(CFLAGS) -c -o $@ $<

test: test_mo5z
	./test_mo5z

clean:
	rm -f mo5z test_mo5z zx0_*.o

.PHONY: test clean
