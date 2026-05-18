CXX      ?= g++
CXXFLAGS ?= -std=c++23 -O2 -Wall -Wextra
LIB      := ../lib

png2mo5: png2mo5.cpp $(LIB)/stb/stb_image.h $(LIB)/stb/stb_image_resize2.h $(LIB)/stb/stb_image_write.h
	$(CXX) $(CXXFLAGS) -I$(LIB) -o $@ $<

test_png2mo5: test_png2mo5.cpp png2mo5.cpp $(LIB)/catch2/catch_amalgamated.cpp
	$(CXX) $(CXXFLAGS) -I$(LIB) -DPNG2MO5_TESTING -o $@ test_png2mo5.cpp $(LIB)/catch2/catch_amalgamated.cpp

test: test_png2mo5
	./test_png2mo5

clean:
	rm -f png2mo5 png2mo5-rgb test_png2mo5

.PHONY: test clean
