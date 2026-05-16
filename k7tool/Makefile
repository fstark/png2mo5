CXX      ?= g++
CXXFLAGS ?= -std=c++23 -O2 -Wall -Wextra

k7tool: k7tool.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

test_k7tool: test_k7tool.cpp k7tool.cpp catch2/catch_amalgamated.cpp
	$(CXX) $(CXXFLAGS) -DK7TOOL_TESTING -o $@ test_k7tool.cpp catch2/catch_amalgamated.cpp

test: test_k7tool
	./test_k7tool

clean:
	rm -f k7tool test_k7tool

.PHONY: test clean
