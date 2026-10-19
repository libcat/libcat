# A release build of the library is what this benchmark needs, because it spends
# its time in the multiply loops.
BUILD ?= ../../build-rel
LIBCAT ?= $(BUILD)/libcat.a

UNAME := $(shell uname -s)

ifeq ($(UNAME),Darwin)
# Homebrew keeps openssl out of the default search path
OPENSSL_CFLAGS := $(shell test -d /opt/homebrew/opt/openssl && echo -I/opt/homebrew/opt/openssl/include)
OPENSSL_LIBS := $(shell test -d /opt/homebrew/opt/openssl && echo -L/opt/homebrew/opt/openssl/lib) -lssl -lcrypto
# POSIX and GNU feature macros hide fd_set and other BSD names on Darwin
PLATFORM_DEFS := -D_FILE_OFFSET_BITS=64
PLATFORM_LIBS := -lpthread -lm
else
OPENSSL_CFLAGS :=
OPENSSL_LIBS := -lssl -lcrypto
PLATFORM_DEFS := -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -D_LARGEFILE_SOURCE -D_POSIX_C_SOURCE=200112
PLATFORM_LIBS := -ldl -pthread -lm
endif

CC ?= cc
CFLAGS ?= -O3 -march=native -DNDEBUG
CFLAGS += -std=gnu11 -Wall -Wextra
DEFS := -DHAVE_LIBCAT=1 -DCAT_USE_THREAD_KEY=1 -DCAT_HAVE_CURL=1 -DCAT_HAVE_OPENSSL=1 \
    -DCAT_HAVE_PQ=1 $(PLATFORM_DEFS)
INCS := -I../../include -I../../deps/libuv/include $(OPENSSL_CFLAGS)
LIBS := $(LIBCAT) $(PLATFORM_LIBS) $(OPENSSL_LIBS)

all: main

main: main.c $(LIBCAT)
	$(CC) $(CFLAGS) $(DEFS) $(INCS) -o $@ main.c $(LIBS)

$(LIBCAT):
	cmake -S ../.. -B $(BUILD) -DCMAKE_BUILD_TYPE=Release -DLIBCAT_USE_THREAD_KEY=1
	cmake --build $(BUILD) --target cat -- -j$$(nproc)

clean:
	rm -f main

.PHONY: all clean
