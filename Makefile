OBJECTS=main.o run.o shared.o fuse_sandbox.o path.o
TARGET=sandbox

BASE_CXXFLAGS=`pkg-config --cflags fuse` -g -std=c++11
override CXXFLAGS:=$(BASE_CXXFLAGS) $(CXXFLAGS)

BASE_LDFLAGS=`pkg-config --libs fuse`
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
