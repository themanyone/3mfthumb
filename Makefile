NAME=3mfthumb
CFLAGS=-g -Wall  $(shell pkg-config --cflags lib3mf) 
LDFLAGS:=$(shell pkg-config --libs lib3mf) $(LIBS)

CXX?=g++

all:
	${CXX} ${CFLAGS} -c ${NAME}.cpp -o ${NAME}.o
	${CXX} ${NAME}.o -o ${NAME} ${LDFLAGS}

install: 
	ln -sf "$(shell pwd)/3mfthumb" ~/.local/bin/
	cp -lf 3mf.thumbnailer /home/k/.local/share/thumbnailers/
	rm -rf ~/.cache/thumbnails

clean:
	rm -f *.o ${NAME}
