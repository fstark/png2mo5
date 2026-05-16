CXX      ?= g++
CXXFLAGS ?= -std=c++23 -O2 -Wall -Wextra
LIB      := ../lib

mo5z2png: mo5z2png.cpp $(LIB)/stb/stb_image_write.h
	$(CXX) $(CXXFLAGS) -I$(LIB) -o $@ $<

clean:
	rm -f mo5z2png

.PHONY: clean
