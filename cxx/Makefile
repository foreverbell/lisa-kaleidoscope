all: lisa

lisa: lisa.cc jpeg.hpp rio_socket.hpp
	c++ lisa.cc -o lisa -ljpeg -lpthread -std=c++14 -O2

run: lisa
	./lisa

clean:
	rm lisa

.PHONY: run clean
