# ZenFS utility makefile

TARGET = zenfs

CC ?= gcc
CXX ?= g++

CPPFLAGS = $(shell pkg-config --cflags rocksdb)
LIBS = $(shell pkg-config --static --libs rocksdb)

all: $(TARGET)

$(TARGET): $(TARGET).cc
	$(CXX) $(CPPFLAGS) -o $(TARGET) $< $(LIBS)

clean:
	$(RM) $(TARGET)
