# Makefile for http parser demo
# this is linux/gmake/x86-64 only, im so lazy to implement it for other os

CC = cc
CFLAGS = -O3 -Ofast
CFLAGS_COMMON = -g -fno-omit-frame-pointer -fvisibility=hidden -fPIC -std=gnu90 -fdiagnostics-show-option -fno-omit-frame-pointer -fno-optimize-sibling-calls -fstack-protector -fcommon
CFLAGS_OBJ = $(CFLAGS) $(CFLAGS_COMMON)
LIBS = -lpthread -lrt -ldl
LDFLAGS =
LDFLAGS_COMMON = -g
LDFLAGS_OBJ = $(CFLAGS) $(LDFLAGS_COMMON)
BUILD_DIR = build
CAT_ROOT = ../..

all:
	@echo see Makefile for usage >&2

MPP_OBJS = multipart_parser.o
MPP_SRCDIR = deps/multipart-parser-c
MPP_CFLAGS = -I$(CAT_ROOT)/deps/multipart-parser-c
$(BUILD_DIR)/$(MPP_SRCDIR):
	mkdir -p $@
MPP_OBJS_BUILD = $(patsubst %,$(BUILD_DIR)/$(MPP_SRCDIR)/%,$(MPP_OBJS))
$(MPP_OBJS_BUILD): $(BUILD_DIR)/$(MPP_SRCDIR)/%.o: $(CAT_ROOT)/$(MPP_SRCDIR)/%.c $(BUILD_DIR)/$(MPP_SRCDIR)
	$(CC) $(CFLAGS_OBJ) $(MPP_CFLAGS) -c $< -o $@

LLHTTP_OBJS = api.o http.o llhttp.o
LLHTTP_SRCDIR = deps/llhttp/src
LLHTTP_CFLAGS = -I$(CAT_ROOT)/deps/llhttp/include
$(BUILD_DIR)/$(LLHTTP_SRCDIR):
	mkdir -p $@
LLHTTP_OBJS_BUILD = $(patsubst %,$(BUILD_DIR)/$(LLHTTP_SRCDIR)/%,$(LLHTTP_OBJS))
$(LLHTTP_OBJS_BUILD): $(BUILD_DIR)/$(LLHTTP_SRCDIR)/%.o: $(CAT_ROOT)/$(LLHTTP_SRCDIR)/%.c $(BUILD_DIR)/$(LLHTTP_SRCDIR)
	$(CC) $(CFLAGS_OBJ) $(LLHTTP_CFLAGS) -c $< -o $@

# this part is unix(Linux) only
UV_OBJS = fs-poll.o idna.o inet.o random.o strscpy.o strtok.o threadpool.o timer.o uv-common.o uv-data-getter-setters.o version.o unix/async.o unix/core.o unix/dl.o unix/fs.o unix/getaddrinfo.o unix/getnameinfo.o unix/loop-watcher.o unix/loop.o unix/pipe.o unix/poll.o unix/process.o unix/random-devurandom.o unix/signal.o unix/stream.o unix/tcp.o unix/thread.o unix/tty.o unix/udp.o unix/proctitle.o unix/linux.o unix/procfs-exepath.o unix/random-getrandom.o unix/random-sysctl-linux.o
UV_SRCDIR = deps/libuv/src
UV_CFLAGS = -I$(CAT_ROOT)/deps/libuv/include -I$(CAT_ROOT)/deps/libuv/src -DHAVE_LIBCAT=1 -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -D_LARGEFILE_SOURCE -D_POSIX_C_SOURCE=200112
$(BUILD_DIR)/$(UV_SRCDIR) $(BUILD_DIR)/$(UV_SRCDIR)/unix:
	mkdir -p $@
UV_OBJS_BUILD = $(patsubst %,$(BUILD_DIR)/$(UV_SRCDIR)/%,$(UV_OBJS))
$(UV_OBJS_BUILD): $(BUILD_DIR)/$(UV_SRCDIR)/%.o: $(CAT_ROOT)/$(UV_SRCDIR)/%.c $(BUILD_DIR)/$(UV_SRCDIR) $(BUILD_DIR)/$(UV_SRCDIR)/unix
	$(CC) $(CFLAGS_OBJ) $(UV_CFLAGS) -c $< -o $@

ASM_OBJS = jump_x86_64_sysv_elf_gas.o make_x86_64_sysv_elf_gas.o
ASM_SRCDIR = deps/context/asm
$(BUILD_DIR)/$(ASM_SRCDIR):
	mkdir -p $@
ASM_OBJS_BUILD = $(patsubst %,$(BUILD_DIR)/$(ASM_SRCDIR)/%,$(ASM_OBJS))
$(ASM_OBJS_BUILD): $(BUILD_DIR)/$(ASM_SRCDIR)/%.o: $(CAT_ROOT)/$(ASM_SRCDIR)/%.S $(BUILD_DIR)/$(ASM_SRCDIR)
	$(CC) $(CFLAGS_OBJ) $(ASM_CFLAGS) -c $< -o $@

LIBCAT_OBJS = cat_cp.o cat_memory.o cat_string.o cat_error.o cat_log.o cat_env.o cat.o cat_api.o cat_coroutine.o cat_channel.o cat_sync.o cat_event.o cat_poll.o cat_time.o cat_socket.o cat_dns.o cat_work.o cat_buffer.o cat_fs.o cat_signal.o cat_os_wait.o cat_async.o cat_watchdog.o cat_process.o cat_http.o cat_websocket.o
LIBCAT_SRCDIR = src
LIBCAT_CFLAGS = -I$(CAT_ROOT)/include $(UV_CFLAGS) $(LLHTTP_CFLAGS) $(MPP_CFLAGS)
$(BUILD_DIR)/$(LIBCAT_SRCDIR):
	mkdir -p $@
LIBCAT_OBJS_BUILD = $(patsubst %,$(BUILD_DIR)/$(LIBCAT_SRCDIR)/%,$(LIBCAT_OBJS))
$(LIBCAT_OBJS_BUILD): $(BUILD_DIR)/$(LIBCAT_SRCDIR)/%.o: $(CAT_ROOT)/$(LIBCAT_SRCDIR)/%.c $(BUILD_DIR)/$(LIBCAT_SRCDIR)
	$(CC) $(CFLAGS_OBJ) $(LIBCAT_CFLAGS) -c $< -o $@

demo: http_parser
	@echo Done building http_parser demo

MAIN_OBJ = http_parser.o
MAIN_OBJS_BUILD = $(patsubst %,$(BUILD_DIR)/%,$(MAIN_OBJ))
MAIN_SRC_BUILD = $(patsubst %.o,%.c,$(MAIN_OBJ))
$(MAIN_OBJS_BUILD): $(MAIN_SRC_BUILD)
	$(CC) $(CFLAGS_OBJ) $(LIBCAT_CFLAGS) -c $< -o $@

http_parser: $(MPP_OBJS_BUILD) $(LLHTTP_OBJS_BUILD) $(LIBCAT_OBJS_BUILD) $(UV_OBJS_BUILD) $(ASM_OBJS_BUILD) $(MAIN_OBJS_BUILD)
	$(CC) $(LDFLAGS_OBJ) $(MPP_OBJS_BUILD) $(LLHTTP_OBJS_BUILD) $(LIBCAT_OBJS_BUILD) $(UV_OBJS_BUILD) $(ASM_OBJS_BUILD) $(MAIN_OBJS_BUILD) $(LIBS) -o $@
