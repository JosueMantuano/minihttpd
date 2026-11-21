CC ?= cc
CPPFLAGS += -Iinclude -D_GNU_SOURCE
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic

TARGET = minihttpd
OBJECTS = src/main.o src/server.o src/http.o src/files.o src/mime.o

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $(OBJECTS) $(LDLIBS)

src/%.o: src/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(TARGET) $(OBJECTS)