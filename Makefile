NAME=3mfthumb
SHELL=/bin/bash
PREFIX?=/usr
.SUFFIXES:.cpp .o
.PHONY: install uninstall clean rpm deb
SRCS=$(wildcard *.cpp)
OBJS=$(SRCS:.cpp=.o)
HEADERS=$(wildcard *.h win-shellext/*.h)
CFLAGS?=-g -Wall -O2 -std=c++17
BUILD_CFLAGS:=$(shell pkg-config --cflags lib3mf cairo)
LDFLAGS:=$(shell pkg-config --libs lib3mf cairo)

all: $(NAME)

# Install under PREFIX (root, or an explicit PREFIX=...): that is the only
# place the GNOME thumbnail sandbox can reach. A home-directory install is
# refused, see the install/uninstall targets.
DESTDIRS=$(PREFIX)

%.o: %.cpp $(HEADERS)
	$(CXX) $(BUILD_CFLAGS) $(CFLAGS) -c $< -o $@

$(NAME): $(OBJS)
	$(CXX) $(CFLAGS) $(NAME).o -o $(NAME) $(LDFLAGS)

# Nautilus 43+ ignores $XDG_DATA_HOME and only reads thumbnailer entries from
# /usr/share/thumbnailers. GNOME's thumbnail sandbox also mounts only /usr
# (plus /proc and /dev) and runs the Exec program by bare name through the
# caller's PATH, so an install under ~/.local or /usr/local can never be
# found: the binary has to live in /usr/bin.
SYSTEM_THUMBNAILER=$(PREFIX)/share/thumbnailers/3mf.thumbnailer

# KDE's Dolphin and file dialogs ignore freedesktop .thumbnailer entries and
# load thumbnail plugins instead. kde-shellext/ builds that plugin with CMake
# (Qt 6 + KF6 KIO); make builds it when cmake says it can, and installs it
# into Qt's plugin directory next to 3mfthumbnail.json.
KDE_BUILD=build-kde
QT_PLUGIN_LIBDIR:=$(shell pkg-config --variable=libdir Qt6Core 2>/dev/null || echo $(PREFIX)/lib64)
QT_PLUGIN_DEST=$(PREFIX)/$(patsubst $(PREFIX)/%,%,$(QT_PLUGIN_LIBDIR))/qt6/plugins

# Restart Nautilus so it re-reads the thumbnailer entries; it only does so at
# startup. `nautilus -q` needs the desktop user's session bus, and sudo
# replaces that with its own, so the whole recipe runs in one shell: it works
# as the invoking user (SUDO_USER) through setpriv, and says so when
# it cannot, because a root shell has no session to act for. setpriv only
# takes a program, so the bus address goes through `env`.
NAUTILUS_RESTART = \
	me=$${SUDO_USER:-$$(id -un)}; \
	if [ "$$me" != root ] && [ -S /run/user/$$(id -u "$$me")/bus ]; then \
		user=$$me; \
	else \
		user=$$(loginctl list-sessions --no-legend 2>/dev/null | \
			awk '$$3 != "" && $$3 != "-" && $$4 == "user" { print $$3; exit }'); \
	fi; \
	case "$$user" in \
	""|root) echo "Run 'nautilus -q' in your desktop session to restart Nautilus.";; \
	*) setpriv --reuid="$$user" --regid="$$(id -gn "$$user")" --clear-groups \
		--reset-env env \
		DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/$$(id -u "$$user")/bus" \
		sh -c 'command -v nautilus >/dev/null && nautilus -q' \
		&& echo "Restarted Nautilus for $$user." \
		|| echo "Run 'nautilus -q' in your desktop session to restart Nautilus.";; \
	esac

# A home-directory install does not work in sandboxed file managers: Nautilus
# runs thumbnailers inside bubblewrap, which mounts only /usr (+ /proc, /dev)
# and resolves the Exec program through the caller's PATH, so nothing under
# $HOME is reachable. It cannot exec the program, and instead of showing no
# preview it locks up or freezes. Until Nautilus changes, a .thumbnailer in
# ~/.local/share/thumbnailers only helps if its Exec points at a program the
# sandbox can reach, such as /usr/bin/3mfthumb, which is why unprivileged
# installs and uninstalls are refused here.
REFUSE_UNPRIVILEGED = \
	echo "3mfthumb: unprivileged installs are not supported."; \
	echo "Nautilus runs thumbnailers in a bubblewrap sandbox that cannot exec"; \
	echo "programs from home directories, and it freezes instead of showing"; \
	echo "no preview. Until Nautilus changes, a *.thumbnailer in"; \
	echo "~/.local/share/thumbnailers only works if it points at an executable"; \
	echo "the sandbox can reach, such as /usr/bin/3mfthumb."; \
	echo "Install for every user with 'sudo make install'."; \
	exit 1

install:
ifeq ($(ROOT_INSTALL),0)
	@$(REFUSE_UNPRIVILEGED)
else
	$(info Installing to $(DESTDIRS))
	for d in $(DESTDIRS); do \
		install -Dm 755 $(NAME) $$d/bin/$(NAME) && \
		install -Dm 644 3mf.thumbnailer $$d/share/thumbnailers/3mf.thumbnailer || exit 1; \
	done
	install -D -m 644 3mf.thumbnailer $(SYSTEM_THUMBNAILER)
	@if command -v cmake >/dev/null 2>&1 && \
		cmake -S kde-shellext -B $(KDE_BUILD) >/dev/null 2>&1; then \
		cmake --build $(KDE_BUILD) && cmake --install $(KDE_BUILD); \
	else \
		echo "3mfthumb: KDE thumbnail plugin not built (needs cmake, Qt 6 and KF6 KIO)"; \
	fi
	@$(NAUTILUS_RESTART)
endif

uninstall:
ifeq ($(ROOT_INSTALL),0)
	@$(REFUSE_UNPRIVILEGED)
else
	for d in $(DESTDIRS); do \
		$(RM) $$d/bin/$(NAME) $$d/share/thumbnailers/3mf.thumbnailer; \
	done
	$(RM) $(SYSTEM_THUMBNAILER)
	$(RM) $(QT_PLUGIN_DEST)/thumbcreator/lib3mfthumbnail.so \
		$(QT_PLUGIN_DEST)/thumbcreator/3mfthumbnail.json
	$(RM) -r $(KDE_BUILD)
	@$(NAUTILUS_RESTART)
endif


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
	@# A cached archive outlives a tag that moved, and Source0 is the tag:
	@# rebuild it from the local tag, keeping the cached copy only when its
	@# contents match. GitHub re-archives the tag, so bytes differ.
	@if git cat-file -e refs/tags/v$(RPM_VERSION) 2>/dev/null; then \
		echo "building $(RPM_ARCHIVE) from tag v$(RPM_VERSION)"; \
		git archive --format=tar.gz --prefix=$(NAME)-$(RPM_VERSION)/ \
			-o $(RPM_ARCHIVE).new v$(RPM_VERSION); \
		if [ -f $(RPM_ARCHIVE) ] && \
		   cmp -s <(tar xzOf $(RPM_ARCHIVE) 2>/dev/null) \
			  <(tar xzOf $(RPM_ARCHIVE).new) 2>/dev/null; then \
			echo "using $(RPM_ARCHIVE)"; rm -f $(RPM_ARCHIVE).new; \
		else \
			mv -f $(RPM_ARCHIVE).new $(RPM_ARCHIVE); \
		fi; \
	elif [ -f $(RPM_ARCHIVE) ]; then echo "using $(RPM_ARCHIVE)"; else \
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
	$(RM) -r $(KDE_BUILD)
