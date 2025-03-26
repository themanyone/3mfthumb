NAME=3mfthumb
SHELL=/bin/sh
PREFIX?=/usr
INSTALL_HOME=$(HOME)/.local
INSTALL_ROOT=$(PREFIX)/local
DESTDIR=$(INSTALL_HOME)
.SUFFIXES:.cpp .o
.PHONY: install clean
SRCS=$(wildcard *.cpp)
OBJS=$(SRCS:.cpp=.o)
CFLAGS?=-g -Wall -O2
BUILD_CFLAGS:=$(shell pkg-config --cflags lib3mf)
LDFLAGS:=$(shell pkg-config --libs lib3mf)
ifeq ($(shell whoami), root)
    DESTDIR:=$(INSTALL_ROOT)
endif

all: $(NAME)

%.o: %.cpp
	$(CXX) $(BUILD_CFLAGS) $(CFLAGS) -c $< -o $@

$(NAME): $(OBJS)
	$(CXX) $(CFLAGS) $(NAME).o -o $(NAME) $(LDFLAGS)

install:
	$(info Installing to $(DESTDIR))
	install -Ds $(NAME) $(DESTDIR)/bin/
	install -D -m 644 3mf.thumbnailer $(DESTDIR)/share/thumbnailers/
	$(RM) -rf $(HOME)/.cache/thumbnails/*

uninstall:
	$(RM)  $(DESTDIR)/bin/$(NAME)
	$(RM)  $(DESTDIR)/share/thumbnailers/3mf.thumbnailer

clean:
	$(RM) *.o $(NAME)
