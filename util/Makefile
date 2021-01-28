# ZenFS utility makefile

TARGET = zenfs

CC=g++
CPPFLAGS = $(shell pkg-config --cflags rocksdb)
LIBS = $(shell pkg-config --static --libs rocksdb)

all: $(TARGET)

$(TARGET): $(TARGET).cc
	$(CC) $(CPPFLAGS)  -o $(TARGET) $< $(LIBS)

clean:
	$(RM) $(TARGET)

