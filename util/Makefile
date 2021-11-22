# ZenFS utility makefile

TARGET = zenfs

CC ?= gcc
CXX ?= g++
OBJCOPY ?= objcopy

CXXFLAGS := $(shell pkg-config --cflags rocksdb)
ifneq ($(.SHELLSTATUS),0)
$(error pkg-config failed)
endif

LIBS := $(shell pkg-config --static --libs rocksdb)
ifneq ($(.SHELLSTATUS),0)
$(error pkg-config failed)
endif

CXXFLAGS +=  $(EXTRA_CXXFLAGS)
LDFLAGS +=  $(EXTRA_LDFLAGS)

all: $(TARGET) $(TARGET).dbg

$(TARGET).dbg: $(TARGET)
	@$(OBJCOPY) --only-keep-debug $(TARGET) $(TARGET).dbg
	@$(OBJCOPY) --strip-all $(TARGET)

debug: $(TARGET)

$(TARGET): $(TARGET).cc
	$(CXX) $(CXXFLAGS) -g -o $(TARGET) $< $(LIBS) $(LDFLAGS)

clean:
	$(RM) $(TARGET) $(TARGET).dbg
