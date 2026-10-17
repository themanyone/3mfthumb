NAME=3mfthumb
SHELL=/bin/sh
PREFIX?=/usr
INSTALL_HOME=$(HOME)/.local
INSTALL_ROOT=$(PREFIX)/local
DESTDIR=$(INSTALL_HOME)
.SUFFIXES:.cpp .o
.PHONY: install uninstall clean rpm deb
SRCS=$(wildcard *.cpp)
OBJS=$(SRCS:.cpp=.o)
CFLAGS?=-g -Wall -O2 -std=c++17
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

RPM_VERSION=$(shell sed -n 's/^Version:[[:space:]]*//p' $(NAME).spec)
RPM_TOPDIR?=$(HOME)/rpmbuild
RPM_DEFINES=--define '_topdir $(RPM_TOPDIR)' \
	--define '_sourcedir $(RPM_TOPDIR)/SOURCES' \
	--define '_specdir $(CURDIR)'
RPM_ARCHIVE=$(RPM_TOPDIR)/SOURCES/$(NAME)-$(RPM_VERSION).tar.gz
RPM_TOOLS=rpmbuild spectool

rpm:
	for tool in $(RPM_TOOLS); do command -v $$tool >/dev/null || \
		{ echo "$$tool is required: sudo dnf install rpm-build"; exit 1; }; done
	@mkdir -p $(dir $(RPM_ARCHIVE))
	@if [ -f $(RPM_ARCHIVE) ]; then echo "using $(RPM_ARCHIVE)"; else \
		spectool -g --directory $(dir $(RPM_ARCHIVE)) \
			--define '_topdir $(RPM_TOPDIR)' $(NAME).spec || \
		{ echo "cannot download the upstream release archive"; exit 1; }; fi
	rpmbuild -ba $(RPM_DEFINES) \
		--define '_rpmdir $(RPM_TOPDIR)/RPMS' \
		--define '_srcrpmdir $(RPM_TOPDIR)/SRPMS' \
		$(NAME).spec
	@echo "Built packages in $(RPM_TOPDIR)/RPMS and $(RPM_TOPDIR)/SRPMS"

deb:
	sh ./make-deb.sh

clean:
	$(RM) *.o $(NAME)
