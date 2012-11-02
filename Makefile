SRCDIR=$(dir $(MAKEFILE_LIST))
VPATH=$(SRCDIR)

OBJECTS=main.o run.o shared.o fuse_sandbox.o path.o
TARGET=sandbox

VERSION=$(shell cat $(SRCDIR)/VERSION)

FUSE_CXXFLAGS=$(shell pkg-config --cflags fuse)
BASE_CXXFLAGS=$(FUSE_CXXFLAGS) -g -std=c++11
override CXXFLAGS:=$(BASE_CXXFLAGS) $(CXXFLAGS)

FUSE_LDFLAGS=$(shell pkg-config --libs fuse)
BASE_LDFLAGS=$(FUSE_LDFLAGS)
override LDFLAGS:=$(BASE_LDFLAGS) $(LDFLAGS)

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

clean:
	rm -f $(OBJECTS)

distclean: clean
	rm -f $(TARGET)

dist:
	git archive --remote=$(SRCDIR) --prefix=sandbox-$(VERSION)/ --format=tar HEAD | gzip > sandbox-$(VERSION).tar.gz
