OBJECTS=main.o run.o shared.o fuse_sandbox.o
TARGET=sandbox
CFLAGS:=`pkg-config --cflags fuse` -std=c99 $(CFLAGS)
LDFLAGS:=`pkg-config --libs fuse` $(LDFLAGS)

$(TARGET): $(OBJECTS)
	$(CC) -o$(TARGET) $(LDFLAGS) $(OBJECTS) $(LOADLIBES) $(LDLIBS)

main.o: main.c shared.h
run.o: run.c run.h shared.h
shared.o: shared.c shared.h
fuse_sandbox.o: fuse_sandbox.h fuse_sandbox.c

setcaps: $(TARGET)
	@echo Root password is required to set capabilities
	su -c "setcap cap_sys_admin,cap_sys_chroot+pe $(TARGET)"

clean:
	rm -f $(OBJECTS)

distclean: clean
	rm -f $(TARGET)
