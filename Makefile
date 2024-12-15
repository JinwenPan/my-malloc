CXX ?= g++
CXXFLAGS ?= -g -Wall -O2

all: libmymalloc.so

libmymalloc.so: memory.cpp
	$(CXX) $(CXXFLAGS) -shared -fPIC -ldl -o $@ $<

clean:
	rm -rf *.so *.so.* *.o