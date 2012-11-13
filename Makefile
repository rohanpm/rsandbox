prefix=/usr/local
exec_prefix=$(prefix)
bindir=$(exec_prefix)/bin
datarootdir=$(prefix)/share
mandir=$(datarootdir)/man
man1dir=$(mandir)/man1
INSTALL=install
INSTALL_PROGRAM=$(INSTALL) -D
INSTALL_DATA=$(INSTALL) -D -m 644

SRCDIR=$(dir $(MAKEFILE_LIST))
VPATH=$(SRCDIR)

CAPS=cap_sys_admin,cap_sys_chroot
OBJECTS=main.o run.o shared.o fuse_sandbox.o path.o
TARGET=rsandbox

VERSION=$(shell cat $(SRCDIR)/VERSION)

FUSE_CXXFLAGS=$(shell pkg-config --cflags fuse)
BASE_CXXFLAGS=$(FUSE_CXXFLAGS) -g -std=c++0x
override CXXFLAGS:=$(BASE_CXXFLAGS) $(CXXFLAGS)

FUSE_LDLIBS=$(shell pkg-config --libs fuse)
override LDLIBS:=$(FUSE_LDLIBS) $(LDLIBS)

$(TARGET): $(OBJECTS)
	$(CXX) -o$(TARGET) $(LDFLAGS) $(OBJECTS) $(LOADLIBES) $(LDLIBS)

main.o: main.cpp shared.h
run.o: run.cpp run.h shared.h
shared.o: shared.cpp shared.h
fuse_sandbox.o: fuse_sandbox.cpp fuse_sandbox.h path.h
path.o: path.cpp path.h

setcaps: $(TARGET)
	@echo Root password is required to set capabilities
	su -c "setcap $(CAPS)+pe $(TARGET)"

$(TARGET).1: README.asciidoc
	a2x -vv --no-xmllint --xsltproc-opts=--nonet -d manpage -f manpage -D . $(SRCDIR)/README.asciidoc

install: install-target install-man

install-target: $(TARGET)
	$(INSTALL_PROGRAM) $(TARGET) $(DESTDIR)$(bindir)/$(TARGET)

install-man: $(TARGET).1
	$(INSTALL_DATA) $(TARGET).1 $(DESTDIR)$(man1dir)/$(TARGET).1

clean:
	rm -f $(OBJECTS) README.xml

distclean: clean
	rm -f $(TARGET) $(TARGET).1 rsandbox-$(VERSION).tar.gz

dist:
	git archive --remote=$(SRCDIR) --prefix=rsandbox-$(VERSION)/ --format=tar HEAD | gzip > rsandbox-$(VERSION).tar.gz

help:
	@echo "Makefile for rsandbox"
	@echo ""
	@echo "Supported targets: (default: rsandbox)"
	@echo ""
	@echo "  rsandbox        Compile. Requires g++, FUSE headers, pkg-config."
	@echo "                  Compile flags may be set by CXXFLAGS."
	@echo "                  Link flags may be set by LDLIBS."
	@echo "  rsandbox.1      Generate man page. Requires asciidoc."
	@echo "  setcaps         Set the needed capabilities on rsandbox ($(CAPS));"
	@echo "                  requires root permission and the 'setcap' command."
	@echo "  dist            Create source tarball from git repository."
	@echo "  install         Install into \$$(DESTDIR)\$$(prefix) (default: $(prefix))"
	@echo "  clean           Remove intermediate build artifacts."
	@echo "  distclean       Remove all build artifacts."
	@echo ""
