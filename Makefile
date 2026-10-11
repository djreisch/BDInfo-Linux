# BDInfo for Linux - native build.
#
#   make                 build ./bdinfo (a single, statically linked executable)
#   make install         install to $(PREFIX)/bin (default /usr/local)
#   make PREFIX=~/.local install
#
# Needs only a C++17 compiler and GNU make; the result has no runtime
# dependencies (no .NET, no shared libraries).

CXX      ?= g++
PREFIX   ?= /usr/local
BINDIR   ?= $(PREFIX)/bin

VERSION  ?= $(shell cat VERSION 2>/dev/null)
ifeq ($(strip $(VERSION)),)
VERSION  := unknown
endif

CXXFLAGS ?= -O2
# -fwrapv: the ported C# code relies on wrapping signed integer arithmetic.
override CXXFLAGS += -std=c++17 -fwrapv -Wall -Wextra -DBDINFO_VERSION='"$(VERSION)"'
LDFLAGS  ?= -static -s
override LDFLAGS += -pthread

SRC := $(wildcard src/*.cpp)
OBJ := $(SRC:src/%.cpp=build/%.o)
HDR := $(wildcard src/*.h)

.PHONY: all clean install uninstall

all: bdinfo

bdinfo: $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJ) $(LDFLAGS)

build/%.o: src/%.cpp $(HDR) | build
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# The version is compiled into main.cpp.
build/main.o: VERSION

build:
	mkdir -p build

install: bdinfo
	install -Dm755 bdinfo $(DESTDIR)$(BINDIR)/bdinfo

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/bdinfo

clean:
	rm -rf build bdinfo
