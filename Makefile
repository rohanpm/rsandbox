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
	su -c "setcap cap_sys_admin,cap_sys_chroot+pe $(TARGET)"

$(TARGET).1: README.asciidoc
	a2x -d manpage -f manpage -D . $(SRCDIR)/README.asciidoc

install: install-target install-man

install-target: $(TARGET)
	$(INSTALL_PROGRAM) $(TARGET) $(DESTDIR)$(bindir)/$(TARGET)

install-man: $(TARGET).1
	$(INSTALL_DATA) $(TARGET).1 $(DESTDIR)$(man1dir)/$(TARGET).1

clean:
	rm -f $(OBJECTS)

distclean: clean
	rm -f $(TARGET)

dist:
	git archive --remote=$(SRCDIR) --prefix=rsandbox-$(VERSION)/ --format=tar HEAD | gzip > rsandbox-$(VERSION).tar.gz
