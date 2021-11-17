# ZenFS utility makefile

TARGET = zenfs

CC ?= gcc
CXX ?= g++

CXXFLAGS = $(shell pkg-config --cflags rocksdb)
LIBS = $(shell pkg-config --static --libs rocksdb)

CXXFLAGS +=  $(EXTRA_CXXFLAGS)
LDFLAGS +=  $(EXTRA_LDFLAGS)

all: $(TARGET)

$(TARGET): $(TARGET).cc
	$(CXX) $(CXXFLAGS) -o $(TARGET) $< $(LIBS) $(LDFLAGS)

clean:
	$(RM) $(TARGET)
