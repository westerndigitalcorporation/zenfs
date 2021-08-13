# ZenFS utility makefile

TARGET = zenfs

CC ?= gcc
CXX ?= g++

CPPFLAGS = $(shell pkg-config --cflags rocksdb)
LIBS = $(shell pkg-config --static --libs rocksdb)

all: $(TARGET) zbd_micro_bench

$(TARGET): $(TARGET).cc
	$(CXX) $(CPPFLAGS) -o $(TARGET) $< $(LIBS)

zbd_micro_bench: zbd_micro_bench.cc
	$(CXX) $(CPPFLAGS) -o zbd_micro_bench $< -lzbd -lgflags

clean:
	$(RM) $(TARGET)
