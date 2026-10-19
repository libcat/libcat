# Two front ends: main draws one picture into a PNG, main-gui draws the same
# picture in a window that can be zoomed without limit.
#
# main-gui needs SDL3 and is skipped when pkg-config cannot find it.
BUILD ?= ../../build-rel
LIBCAT ?= $(BUILD)/libcat.a

CC ?= cc
CFLAGS ?= -O3 -march=native -DNDEBUG
CFLAGS += -std=gnu11 -Wall -Wextra
DEFS := -DHAVE_LIBCAT=1 -DCAT_USE_THREAD_KEY=1 -DCAT_HAVE_CURL=1 -DCAT_HAVE_OPENSSL=1 \
    -DCAT_HAVE_PQ=1 -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -D_LARGEFILE_SOURCE \
    -D_POSIX_C_SOURCE=200112
INCS := -I../../include -I../../deps/libuv/include -I.
# libquadmath exists only where the compiler has __float128, which is x86 only
QUAD := $(shell printf 'int main(void){__float128 x = 0; return (int) x;}' | \
    $(CC) -x c - -o /dev/null -lquadmath 2>/dev/null && echo -lquadmath)
LIBS := $(LIBCAT) -ldl -pthread -lm -lz $(QUAD) -lssl -lcrypto

SDL3_CFLAGS := $(shell pkg-config --cflags sdl3 2>/dev/null)
SDL3_LIBS := $(shell pkg-config --libs sdl3 2>/dev/null)

HEADERS := mandel.h png.h pool.h

all: main $(if $(SDL3_LIBS),main-gui)

main: main.c $(HEADERS) $(LIBCAT)
	$(CC) $(CFLAGS) $(DEFS) $(INCS) -o $@ main.c $(LIBS)

main-gui: main-gui.c $(HEADERS) $(LIBCAT)
	$(CC) $(CFLAGS) $(DEFS) $(INCS) $(SDL3_CFLAGS) -o $@ main-gui.c $(LIBS) $(SDL3_LIBS)

$(LIBCAT):
	cmake -S ../.. -B $(BUILD) -DCMAKE_BUILD_TYPE=Release -DLIBCAT_USE_THREAD_KEY=1
	cmake --build $(BUILD) --target cat -- -j$$(nproc)

clean:
	rm -f main main-gui

.PHONY: all clean
