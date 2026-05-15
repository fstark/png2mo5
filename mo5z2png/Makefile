CXX      ?= g++
CXXFLAGS ?= -std=c++23 -O2 -Wall -Wextra

mo5z2png: mo5z2png.cpp stb/stb_image_write.h
	$(CXX) $(CXXFLAGS) -o $@ $<

clean:
	rm -f mo5z2png

.PHONY: clean
