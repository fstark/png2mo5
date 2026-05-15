CXX      ?= g++
CC       ?= gcc
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
CFLAGS   ?= -std=c11 -O2 -Wall -Wextra

ZX0_SRCS = zx0/compress.c zx0/optimize.c zx0/memory.c
ZX0_OBJS = $(ZX0_SRCS:.c=.o)

mo5z: mo5z.cpp $(ZX0_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $< $(ZX0_OBJS)

test_mo5z: test_mo5z.cpp mo5z.cpp $(ZX0_OBJS) catch2/catch_amalgamated.cpp
	$(CXX) $(CXXFLAGS) -DMO5Z_TESTING -o $@ test_mo5z.cpp catch2/catch_amalgamated.cpp $(ZX0_OBJS)

zx0/%.o: zx0/%.c zx0/zx0.h
	$(CC) $(CFLAGS) -c -o $@ $<

test: test_mo5z
	./test_mo5z

clean:
	rm -f mo5z test_mo5z zx0/*.o

.PHONY: test clean
