LIBS = dbus-1 dbus-glib-1 glib-2.0 gio-2.0 bluez
CFLAGS = -c -Iinclude `pkg-config --cflags $(LIBS)` -Wall
LDFLAGS = `pkg-config --libs $(LIBS)`
DEPS = include/myo-bluez.h include/myo-bluetooth/myohw.h
SOURCES = myo-bluez.c myo-bluez_client.c
OBJECTS = $(SOURCES:.c=.o)

.PHONY: clean all debug

all: myo-bluez

debug: CFLAGS += -DDEBUG -g
debug: myo-bluez

%.o: %.c $(DEPS)

myo-bluez: $(OBJECTS)
	$(CC) $(OBJECTS) $(LDFLAGS) -o myo-bluez

clean:
	rm -f *.o
