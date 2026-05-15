CXX      ?= g++
CXXFLAGS ?= -std=c++23 -O2 -Wall -Wextra

png2mo5: png2mo5.cpp stb/stb_image.h stb/stb_image_resize2.h stb/stb_image_write.h
	$(CXX) $(CXXFLAGS) -o $@ $<

test_png2mo5: test_png2mo5.cpp png2mo5.cpp catch2/catch_amalgamated.cpp
	$(CXX) $(CXXFLAGS) -DPNG2MO5_TESTING -o $@ test_png2mo5.cpp catch2/catch_amalgamated.cpp

test: test_png2mo5
	./test_png2mo5

clean:
	rm -f png2mo5 test_png2mo5

.PHONY: test clean
