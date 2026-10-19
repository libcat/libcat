# A release build of the library is what this benchmark needs, because it spends
# its time in the multiply loops.
BUILD ?= ../../build-rel
LIBCAT ?= $(BUILD)/libcat.a

CC ?= cc
CFLAGS ?= -O3 -march=native -DNDEBUG
CFLAGS += -std=gnu11 -Wall -Wextra
DEFS := -DHAVE_LIBCAT=1 -DCAT_USE_THREAD_KEY=1 -DCAT_HAVE_CURL=1 -DCAT_HAVE_OPENSSL=1 \
    -DCAT_HAVE_PQ=1 -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -D_LARGEFILE_SOURCE \
    -D_POSIX_C_SOURCE=200112
INCS := -I../../include -I../../deps/libuv/include
LIBS := $(LIBCAT) -ldl -pthread -lm -lssl -lcrypto

all: main

main: main.c $(LIBCAT)
	$(CC) $(CFLAGS) $(DEFS) $(INCS) -o $@ main.c $(LIBS)

$(LIBCAT):
	cmake -S ../.. -B $(BUILD) -DCMAKE_BUILD_TYPE=Release -DLIBCAT_USE_THREAD_KEY=1
	cmake --build $(BUILD) --target cat -- -j$$(nproc)

clean:
	rm -f main

.PHONY: all clean
