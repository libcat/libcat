cmake_minimum_required(VERSION 3.4)
PROJECT(libcat LANGUAGES C)

cmake_policy(SET CMP0057 NEW) # Enable IN_LIST operator
cmake_policy(SET CMP0064 NEW) # Support if (TEST) operator

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

include(CMakePackageConfigHelpers)
include(CMakeDependentOption)
include(CheckCCompilerFlag)
include(GNUInstallDirs)
include(CTest)

set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_C_STANDARD 90)

# Qemu Build
option(QEMU "build for qemu" OFF)
if(QEMU)
    add_definitions(-D__QEMU__=1)
endif()

option(ASAN "Enable AddressSanitizer (ASan)" OFF)
if(ASAN AND CMAKE_C_COMPILER_ID MATCHES "AppleClang|GNU|Clang")
  add_definitions(-D__ASAN__=1)
  set (CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
  set (CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
endif()

# Compiler check
message(STATUS "C Compiler is ${CMAKE_C_COMPILER_ID}")
string(CONCAT is-msvc $<OR:
    $<C_COMPILER_ID:MSVC>,
    $<STREQUAL:${CMAKE_C_COMPILER_FRONTEND_VARIANT},MSVC>
>)

set(lint-no-unused-parameter $<$<BOOL:${CAT_LINT_NO_UNUSED_PARAMETER}>:-Wno-unused-parameter>)
set(lint-strict-prototypes $<$<BOOL:${CAT_LINT_STRICT_PROTOTYPES}>:-Wstrict-prototypes>)
set(lint-extra $<$<BOOL:${CAT_LINT_EXTRA}>:-Wextra>)
list(APPEND cat_cflags ${lint-strict-prototypes} ${lint-extra} ${lint-default} ${lint-w4})
list(APPEND cat_cflags ${lint-no-unused-parameter})
if(MSVC)
    check_c_compiler_flag(/W4 CAT_LINT_W4)
    check_c_compiler_flag(/wd4100 CAT_LINT_NO_UNUSED_PARAMETER_MSVC)
    check_c_compiler_flag(/wd4127 CAT_LINT_NO_CONDITIONAL_CONSTANT_MSVC)
    check_c_compiler_flag(/wd4201 CAT_LINT_NO_NONSTANDARD_MSVC)
    check_c_compiler_flag(/wd4206 CAT_LINT_NO_NONSTANDARD_EMPTY_TU_MSVC)
    check_c_compiler_flag(/wd4210 CAT_LINT_NO_NONSTANDARD_FILE_SCOPE_MSVC)
    check_c_compiler_flag(/wd4232 CAT_LINT_NO_NONSTANDARD_NONSTATIC_DLIMPORT_MSVC)
    check_c_compiler_flag(/wd4456 CAT_LINT_NO_HIDES_LOCAL)
    check_c_compiler_flag(/wd4457 CAT_LINT_NO_HIDES_PARAM)
    check_c_compiler_flag(/wd4459 CAT_LINT_NO_HIDES_GLOBAL)
    check_c_compiler_flag(/wd4706 CAT_LINT_NO_CONDITIONAL_ASSIGNMENT_MSVC)
    check_c_compiler_flag(/wd4996 CAT_LINT_NO_UNSAFE_MSVC)

    check_c_compiler_flag(-Wall CAT_LINT_WALL) # DO NOT use this under MSVC

    # TODO: Place these into its own function
    check_c_compiler_flag(-Wno-unused-parameter CAT_LINT_NO_UNUSED_PARAMETER)
    check_c_compiler_flag(-Wstrict-prototypes CAT_LINT_STRICT_PROTOTYPES)
    check_c_compiler_flag(-Wextra CAT_LINT_EXTRA)

    check_c_compiler_flag(/utf-8 CAT_LINT_UTF8_MSVC)

    set(lint-w4 $<$<BOOL:${CAT_LINT_W4}>:/W4>)
    set(lint-no-unused-parameter-msvc $<$<BOOL:${CAT_LINT_NO_UNUSED_PARAMETER_MSVC}>:/wd4100>)
    set(lint-no-conditional-constant-msvc $<$<BOOL:${CAT_LINT_NO_CONDITIONAL_CONSTANT_MSVC}>:/wd4127>)
    set(lint-no-nonstandard-msvc $<$<BOOL:${CAT_LINT_NO_NONSTANDARD_MSVC}>:/wd4201>)
    set(lint-no-nonstandard-empty-tu-msvc $<$<BOOL:${CAT_LINT_NO_NONSTANDARD_EMPTY_TU_MSVC}>:/wd4206>)
    set(lint-no-nonstandard-file-scope-msvc $<$<BOOL:${CAT_LINT_NO_NONSTANDARD_FILE_SCOPE_MSVC}>:/wd4210>)
    set(lint-no-nonstandard-nonstatic-dlimport-msvc $<$<BOOL:${CAT_LINT_NO_NONSTANDARD_NONSTATIC_DLIMPORT_MSVC}>:/wd4232>)
    set(lint-no-hides-local-msvc $<$<BOOL:${CAT_LINT_NO_HIDES_LOCAL}>:/wd4456>)
    set(lint-no-hides-param-msvc $<$<BOOL:${CAT_LINT_NO_HIDES_PARAM}>:/wd4457>)
    set(lint-no-hides-global-msvc $<$<BOOL:${CAT_LINT_NO_HIDES_GLOBAL}>:/wd4459>)
    set(lint-no-conditional-assignment-msvc $<$<BOOL:${CAT_LINT_NO_CONDITIONAL_ASSIGNMENT_MSVC}>:/wd4706>)
    set(lint-no-unsafe-msvc $<$<BOOL:${CAT_LINT_NO_UNSAFE_MSVC}>:/wd4996>)
    # Unfortunately, this one is complicated because MSVC and clang-cl support -Wall
    # but using it is like calling -Weverything
    string(CONCAT lint-default $<
    $<AND:$<BOOL:${CAT_LINT_WALL}>,$<NOT:${is-msvc}>>:-Wall
    >)
    set(lint-utf8-msvc $<$<BOOL:${CAT_LINT_UTF8_MSVC}>:/utf-8>)
    set(cat_msvc_cflags ${lint-utf8-msvc})
    list(APPEND cat_msvc_cflags ${lint-no-unused-parameter-msvc})
    list(APPEND cat_msvc_cflags ${lint-no-conditional-constant-msvc})
    list(APPEND cat_msvc_cflags ${lint-no-nonstandard-msvc})
    list(APPEND cat_msvc_cflags ${lint-no-nonstandard-empty-tu-msvc})
    list(APPEND cat_msvc_cflags ${lint-no-nonstandard-file-scope-msvc})
    list(APPEND cat_msvc_cflags ${lint-no-nonstandard-nonstatic-dlimport-msvc})
    list(APPEND cat_msvc_cflags ${lint-no-hides-local-msvc})
    list(APPEND cat_msvc_cflags ${lint-no-hides-param-msvc})
    list(APPEND cat_msvc_cflags ${lint-no-hides-global-msvc})
    list(APPEND cat_msvc_cflags ${lint-no-conditional-assignment-msvc})
    list(APPEND cat_msvc_cflags ${lint-no-unsafe-msvc})
    list(APPEND cat_cflags ${cat_msvc_cflags})
endif()

# ========== ASM ===========
if (NOT MSVC)
    enable_language(ASM)
    if(CMAKE_ASM_COMPILER_WORKS)
        message(STATUS "ASM enabled")
        message(STATUS "ASM Compiler is ${CMAKE_ASM_COMPILER_ID}")
    else()
        # TODO: use ucontext
        message(FATAL_ERROR "Enable ASM failed")
    endif(CMAKE_ASM_COMPILER_WORKS)
else()
    set(MASMFound FALSE)
    enable_language(ASM_MASM)
    if(CMAKE_ASM_MASM_COMPILER_WORKS)
        SET(MASMFound TRUE)
        message(STATUS "ASM_MASM enabled")
        message(STATUS "ASM_MASM Compiler is ${CMAKE_ASM_MASM_COMPILER_ID}")
    else()
        message(FATAL_ERROR "Enable ASM_MASM failed")
    endif(CMAKE_ASM_MASM_COMPILER_WORKS)
endif()
set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} -x assembler-with-cpp")
# ========== msvc x86 disable safeseh ==========
# if (MSVC AND "${LIBCOPP_FCONTEXT_OS_PLATFORM}" STREQUAL "i386")
# 	set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /SAFESEH:NO")
# 	set (CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /SAFESEH:NO")
# 	set (CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} /SAFESEH:NO")
# endif()

set(cat_defines HAVE_LIBCAT=1)

if (NOT CMAKE_BUILD_TYPE STREQUAL "Release")
	set(CMAKE_BUILD_TYPE Debug)
	set(CAT_DEBUG 1)
    list(APPEND cat_defines CAT_DEBUG=1)
    if(NOT MSVC)
        list(APPEND cat_cflags -g3 -O0)
    else()
        list(APPEND cat_cflags /Od)
    endif()
endif()

set(cat_includes include)

set(cat_sources
    # [internal]
    src/cat_cp.c
    src/cat_memory.c
    src/cat_string.c
    src/cat_error.c
    src/cat_module.c
    src/cat_log.c
    src/cat_env.c
    # [public]
    src/cat.c
    src/cat_api.c
    src/cat_coroutine.c
    src/cat_channel.c
    src/cat_sync.c
    src/cat_event.c
    src/cat_time.c
    src/cat_socket.c
    src/cat_dns.c
    src/cat_work.c
    src/cat_buffer.c
    src/cat_fs.c
    src/cat_signal.c
    src/cat_watch_dog.c
    src/cat_http.c
    src/cat_websocket.c
)

set(cat_libraries)

# ========== Detect mac info (thanks to libcopp) ==========
if (1)
    # ========== set os platform ==========
    # CAT_CPU_ARCH can be set to arm/arm64/i386/x86_64/combined/mips/ppc32/ppc64/sparc/sparc64
    if (NOT CAT_CPU_ARCH)
        if(NOT CMAKE_SYSTEM_PROCESSOR)
            set(CMAKE_SYSTEM_PROCESSOR ${CMAKE_HOST_SYSTEM_PROCESSOR})
        endif()
        string(TOLOWER ${CMAKE_SYSTEM_PROCESSOR} CAT_CPU_ARCH)
    	if(CAT_CPU_ARCH STREQUAL "unknown" OR
    		CAT_CPU_ARCH STREQUAL "i386" OR
    		CAT_CPU_ARCH STREQUAL "i686" OR
    		CAT_CPU_ARCH STREQUAL "x86_64" OR
    		CAT_CPU_ARCH STREQUAL "amd64"
    		)
            if(CMAKE_SIZEOF_VOID_P MATCHES 8)
                set(CMAKE_SYSTEM_PROCESSOR "x86_64")
                set(CAT_CPU_ARCH "x86_64")
            else()
                set(CMAKE_SYSTEM_PROCESSOR "i386")
                set(CAT_CPU_ARCH "i386")
    		endif()
    	elseif(
    		CAT_CPU_ARCH MATCHES "^armv5" OR
    		CAT_CPU_ARCH MATCHES "^armv7" OR
    		CAT_CPU_ARCH MATCHES "^armeabi"
    	)
    		set(CAT_CPU_ARCH "arm")
    	elseif(
    			CAT_CPU_ARCH MATCHES "^arm64" OR
    			CAT_CPU_ARCH MATCHES "^aarch64"
    		)
    			set(CAT_CPU_ARCH "arm64")
        endif()
        if (CAT_CPU_ARCH STREQUAL "unknown")
            # TODO use ucontext
            message(FATAL_ERROR RED "Can not detect the processor, please add -DCAT_CPU_ARCH=arm/arm64/i386/x86_64/combined/mips32/mips64/ppc32/ppc64 to help us to find target processor")
        endif()
    endif()
    # patch for old -DCAT_CPU_ARCH=mips to -DCAT_CPU_ARCH=mips32
    if (CAT_CPU_ARCH STREQUAL "mips")
    	set(CAT_CPU_ARCH "mips32")
    endif ()
    # ========== set abi ==========
    if( CAT_CPU_ARCH STREQUAL "arm" OR CAT_CPU_ARCH STREQUAL "arm64")
        set(CAT_ABI "aapcs")
    elseif( CAT_CPU_ARCH STREQUAL "mips32" )
        set(CAT_ABI "o32")
    elseif( CAT_CPU_ARCH STREQUAL "mips64" )
        set(CAT_ABI "n64")
    elseif (WIN32 OR WINCE OR WINDOWS_PHONE OR WINDOWS_STORE OR MINGW OR CYGWIN)
        set(CAT_ABI "ms")
    else()
        set(CAT_ABI "sysv")
    endif()
    # ========== set binary format ==========
    # CAT_BINARY_FORMAT can be set to elf/macho/pe/xcoff
    if(NOT CAT_BINARY_FORMAT)
        if(WIN32 OR WINCE OR WINDOWS_PHONE OR WINDOWS_STORE OR MINGW OR CYGWIN)
            set(CAT_BINARY_FORMAT "pe")
        elseif(APPLE)
            set(CAT_BINARY_FORMAT "macho")
        elseif(AIX) # cmake not supported now
            set(CAT_BINARY_FORMAT "xcoff")
        else()
            set(CAT_BINARY_FORMAT "elf")
        endif()
    endif()
    # ========== set as tool ==========
    # CAT_ASM_TOOL can be set to gas/armasm/masm
    if(NOT CAT_ASM_TOOL)
        if(MSVC)
            if ("${CAT_CPU_ARCH}" STREQUAL "arm" OR "${CAT_CPU_ARCH}" STREQUAL "arm64")
                set(CAT_ASM_TOOL "armasm")
            else()
                set(CAT_ASM_TOOL "masm")
            endif()
        else()
            set(CAT_ASM_TOOL "gas")
        endif()
        if (WIN32 OR MINGW)
            set(CAT_COROUTINE_CONTEXT_AS_OUTPUT_SUFFIX "obj")
        else()
            set(CAT_COROUTINE_CONTEXT_AS_OUTPUT_SUFFIX "o")
        endif()
    endif()
    # ========== set suffix ==========
    if (WIN32 OR WINCE OR WINDOWS_PHONE OR WINDOWS_STORE OR MINGW OR CYGWIN)
        set(CAT_ASM_SUFFIX "asm")
    else()
        set(CAT_ASM_SUFFIX "S")
    endif()
endif()

# context dep
if (1)
    set(cat_coroutine_context_dir "deps/context/asm")
    set(cat_coroutine_context_name "${CAT_CPU_ARCH}_${CAT_ABI}_${CAT_BINARY_FORMAT}_${CAT_ASM_TOOL}.${CAT_ASM_SUFFIX}")
    message(STATUS "ASM of context is ${cat_coroutine_context_name}")
    set(cat_context_sources
        ${cat_coroutine_context_dir}/make_${cat_coroutine_context_name}
        ${cat_coroutine_context_dir}/jump_${cat_coroutine_context_name}
    )
    add_library(cat_context OBJECT ${cat_context_sources})
endif()

# libuv dep /* $Id: 97a903309f79d79496a2a1f95cbcce21bfc2f76e $ */
if (1)
    set(uv_dir "deps/libuv")

    set(uv_sources
        ${uv_dir}/src/fs-poll.c
        ${uv_dir}/src/idna.c
        ${uv_dir}/src/inet.c
        ${uv_dir}/src/random.c
        ${uv_dir}/src/strscpy.c
        ${uv_dir}/src/threadpool.c
        ${uv_dir}/src/timer.c
        ${uv_dir}/src/uv-common.c
        ${uv_dir}/src/uv-data-getter-setters.c
        ${uv_dir}/src/version.c)

    if(WIN32)
      list(APPEND uv_defines WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0600)
      list(APPEND uv_libraries
           psapi
           user32
           advapi32
           iphlpapi
           userenv
           ws2_32)
      list(APPEND uv_sources
           ${uv_dir}/src/win/async.c
           ${uv_dir}/src/win/core.c
           ${uv_dir}/src/win/detect-wakeup.c
           ${uv_dir}/src/win/dl.c
           ${uv_dir}/src/win/error.c
           ${uv_dir}/src/win/fs.c
           ${uv_dir}/src/win/fs-event.c
           ${uv_dir}/src/win/getaddrinfo.c
           ${uv_dir}/src/win/getnameinfo.c
           ${uv_dir}/src/win/handle.c
           ${uv_dir}/src/win/loop-watcher.c
           ${uv_dir}/src/win/pipe.c
           ${uv_dir}/src/win/thread.c
           ${uv_dir}/src/win/poll.c
           ${uv_dir}/src/win/process.c
           ${uv_dir}/src/win/process-stdio.c
           ${uv_dir}/src/win/signal.c
           ${uv_dir}/src/win/snprintf.c
           ${uv_dir}/src/win/stream.c
           ${uv_dir}/src/win/tcp.c
           ${uv_dir}/src/win/tty.c
           ${uv_dir}/src/win/udp.c
           ${uv_dir}/src/win/util.c
           ${uv_dir}/src/win/winapi.c
           ${uv_dir}/src/win/winsock.c)
    else()
      list(APPEND uv_defines _FILE_OFFSET_BITS=64 _LARGEFILE_SOURCE)
      if(NOT CMAKE_SYSTEM_NAME MATCHES "Android|OS390|QNX")
        # TODO: This should be replaced with find_package(Threads) if possible
        # Android has pthread as part of its c library, not as a separate
        # libpthread.so.
        list(APPEND uv_libraries pthread)
      endif()
      list(APPEND uv_sources
           ${uv_dir}/src/unix/async.c
           ${uv_dir}/src/unix/core.c
           ${uv_dir}/src/unix/dl.c
           ${uv_dir}/src/unix/fs.c
           ${uv_dir}/src/unix/getaddrinfo.c
           ${uv_dir}/src/unix/getnameinfo.c
           ${uv_dir}/src/unix/loop-watcher.c
           ${uv_dir}/src/unix/loop.c
           ${uv_dir}/src/unix/pipe.c
           ${uv_dir}/src/unix/poll.c
           ${uv_dir}/src/unix/process.c
           ${uv_dir}/src/unix/random-devurandom.c
           ${uv_dir}/src/unix/signal.c
           ${uv_dir}/src/unix/stream.c
           ${uv_dir}/src/unix/tcp.c
           ${uv_dir}/src/unix/thread.c
           ${uv_dir}/src/unix/tty.c
           ${uv_dir}/src/unix/udp.c)
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "AIX")
      list(APPEND uv_defines
           _ALL_SOURCE
           _LINUX_SOURCE_COMPAT
           _THREAD_SAFE
           _XOPEN_SOURCE=500
           HAVE_SYS_AHAFS_EVPRODS_H)
      list(APPEND uv_libraries perfstat)
      list(APPEND uv_sources
           ${uv_dir}/src/unix/aix.c
           ${uv_dir}/src/unix/aix-common.c)
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Android")
      list(APPEND uv_defines _GNU_SOURCE)
      list(APPEND uv_libraries dl)
      list(APPEND uv_sources
           ${uv_dir}/src/unix/android-ifaddrs.c
           ${uv_dir}/src/unix/linux-core.c
           ${uv_dir}/src/unix/linux-inotify.c
           ${uv_dir}/src/unix/linux-syscalls.c
           ${uv_dir}/src/unix/procfs-exepath.c
           ${uv_dir}/src/unix/pthread-fixes.c
           ${uv_dir}/src/unix/random-getentropy.c
           ${uv_dir}/src/unix/random-getrandom.c
           ${uv_dir}/src/unix/random-sysctl-linux.c)
    endif()

    if(APPLE OR CMAKE_SYSTEM_NAME MATCHES "Android|Linux|OS390")
      list(APPEND uv_sources ${uv_dir}/src/unix/proctitle.c)
    endif()

    if(CMAKE_SYSTEM_NAME MATCHES "DragonFly|FreeBSD")
      list(APPEND uv_sources ${uv_dir}/ ${uv_dir}/src/unix/freebsd.c)
    endif()

    if(CMAKE_SYSTEM_NAME MATCHES "DragonFly|FreeBSD|NetBSD|OpenBSD")
      list(APPEND uv_sources ${uv_dir}/src/unix/posix-hrtime.c ${uv_dir}/src/unix/bsd-proctitle.c)
    endif()

    if(APPLE OR CMAKE_SYSTEM_NAME MATCHES "DragonFly|FreeBSD|NetBSD|OpenBSD")
      list(APPEND uv_sources ${uv_dir}/src/unix/bsd-ifaddrs.c ${uv_dir}/src/unix/kqueue.c)
    endif()

    if(CMAKE_SYSTEM_NAME MATCHES "FreeBSD")
      list(APPEND uv_sources ${uv_dir}/src/unix/random-getrandom.c)
    endif()

    if(APPLE OR CMAKE_SYSTEM_NAME STREQUAL "OpenBSD")
      list(APPEND uv_sources ${uv_dir}/src/unix/random-getentropy.c)
    endif()

    if(APPLE)
      list(APPEND uv_defines _DARWIN_UNLIMITED_SELECT=1 _DARWIN_USE_64_BIT_INODE=1)
      list(APPEND uv_sources
           ${uv_dir}/src/unix/darwin-proctitle.c
           ${uv_dir}/src/unix/darwin.c
           ${uv_dir}/src/unix/fsevents.c)
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
      list(APPEND uv_defines _GNU_SOURCE _POSIX_C_SOURCE=200112)
      list(APPEND uv_libraries dl rt)
      list(APPEND uv_sources
           ${uv_dir}/src/unix/linux-core.c
           ${uv_dir}/src/unix/linux-inotify.c
           ${uv_dir}/src/unix/linux-syscalls.c
           ${uv_dir}/src/unix/procfs-exepath.c
           ${uv_dir}/src/unix/random-getrandom.c
           ${uv_dir}/src/unix/random-sysctl-linux.c)
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "NetBSD")
      list(APPEND uv_sources ${uv_dir}/src/unix/netbsd.c)
      list(APPEND uv_libraries kvm)
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "OpenBSD")
      list(APPEND uv_sources ${uv_dir}/src/unix/openbsd.c)
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "OS390")
      list(APPEND uv_defines PATH_MAX=255)
      list(APPEND uv_defines _AE_BIMODAL)
      list(APPEND uv_defines _ALL_SOURCE)
      list(APPEND uv_defines _ISOC99_SOURCE)
      list(APPEND uv_defines _LARGE_TIME_API)
      list(APPEND uv_defines _OPEN_MSGQ_EXT)
      list(APPEND uv_defines _OPEN_SYS_FILE_EXT)
      list(APPEND uv_defines _OPEN_SYS_IF_EXT)
      list(APPEND uv_defines _OPEN_SYS_SOCK_EXT3)
      list(APPEND uv_defines _OPEN_SYS_SOCK_IPV6)
      list(APPEND uv_defines _UNIX03_SOURCE)
      list(APPEND uv_defines _UNIX03_THREADS)
      list(APPEND uv_defines _UNIX03_WITHDRAWN)
      list(APPEND uv_defines _XOPEN_SOURCE_EXTENDED)
      list(APPEND uv_sources
           ${uv_dir}/src/unix/pthread-fixes.c
           ${uv_dir}/src/unix/os390.c
           ${uv_dir}/src/unix/os390-syscalls.c)
      list(APPEND uv_cflags -Wc,DLL -Wc,exportall -Wc,xplink)
      list(APPEND uv_libraries -Wl,xplink)
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "OS400")
      list(APPEND uv_defines
           _ALL_SOURCE
           _LINUX_SOURCE_COMPAT
           _THREAD_SAFE
           _XOPEN_SOURCE=500)
      list(APPEND uv_sources
        ${uv_dir}/src/unix/aix-common.c
        ${uv_dir}/src/unix/ibmi.c
        ${uv_dir}/src/unix/no-fsevents.c
        ${uv_dir}/src/unix/posix-poll.c)
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "SunOS")
      list(APPEND uv_defines __EXTENSIONS__ _XOPEN_SOURCE=500)
      list(APPEND uv_libraries kstat nsl sendfile socket)
      list(APPEND uv_sources ${uv_dir}/src/unix/no-proctitle.c ${uv_dir}/src/unix/sunos.c)
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Haiku")
      list(APPEND uv_defines _BSD_SOURCE)
      list(APPEND uv_libraries bsd network)
      list(APPEND uv_sources
          ${uv_dir}/src/unix/haiku.c
          ${uv_dir}/src/unix/bsd-ifaddrs.c
          ${uv_dir}/src/unix/no-fsevents.c
          ${uv_dir}/src/unix/no-proctitle.c
          ${uv_dir}/src/unix/posix-hrtime.c
          ${uv_dir}/src/unix/posix-poll.c)
    endif()

    # TODO: include => public, src => private
    set(uv_includes ${uv_dir}/include ${uv_dir}/src)
    if(MSVC)
        list(APPEND uv_cflags ${cat_msvc_cflags})
        # things that dump warnings in libuv
        # thus we donot maintaince libuv here, we disable them.
        list(APPEND uv_cflags /wd4090 /wd4244 /wd4245 /wd4267 /wd4701 /wd4702)
    endif()
    
    add_library(cat_uv OBJECT ${uv_sources})
    list(APPEND cat_defines ${uv_defines})
    target_compile_definitions(cat_uv PRIVATE ${cat_defines})
    target_compile_options(cat_uv PRIVATE ${uv_cflags})
    list(APPEND cat_includes ${uv_includes})
    target_include_directories(cat_uv PRIVATE ${uv_includes})
    list(APPEND cat_libraries ${uv_libraries})
endif()

# Valgrind headers and libraries finder (UNIX-like only)
#
# Usage of this module as follows:
# 	find_package(Valgrind)
#
# Variables used by this module, they can change the default behaviour and need
# to be set before calling find_package:
#
# VALGRIND_ROOT Set this variable to the root installation of valgrind if the
# module has problems finding the proper installation path.
#
# Variables defined by this module:
# 	Valgrind_FOUND System has valgrind
# 	Valgrind_INCLUDE_DIR where to find valgrind/memcheck.h, etc.
# 	Valgrind_EXECUTABLE the valgrind executable.
# Get hint from environment variable (if any)
if(NOT WIN32)
	if(NOT VALGRIND_ROOT)
	    if (DEFINED ENV{VALGRIND_ROOT})
	        set(VALGRIND_ROOT "$ENV{VALGRIND_ROOT}" CACHE PATH "Valgrind base directory location (optional, used for nonstandard installation paths)")
	    else()
	        set(VALGRIND_ROOT "/usr/")
	    endif()
	    mark_as_advanced(VALGRIND_ROOT)
	endif()
	# Search path for nonstandard locations
	if(VALGRIND_ROOT)
		set(Valgrind_INCLUDE_PATH PATHS "${VALGRIND_ROOT}/include" NO_DEFAULT_PATH)
		set(Valgrind_BINARY_PATH PATHS "${VALGRIND_ROOT}/bin" NO_DEFAULT_PATH)
	endif()
	find_path(Valgrind_INCLUDE_DIR valgrind HINTS ${Valgrind_INCLUDE_PATH})
	find_program(Valgrind_EXECUTABLE NAMES valgrind PATH ${Valgrind_BINARY_PATH})
	include(FindPackageHandleStandardArgs)
	find_package_handle_standard_args(Valgrind DEFAULT_MSG Valgrind_INCLUDE_DIR Valgrind_EXECUTABLE)
	mark_as_advanced(Valgrind_INCLUDE_DIR Valgrind_EXECUTABLE)
endif()

# Valgrind dep (UNIX-like only)
if (NOT WIN32)
	find_package(Valgrind QUIET)
	cmake_dependent_option(LIBCAT_WITH_VALGRIND
		"Build with valgrind by default if build type is Debug and found Valgrind" ON
		"${CAT_DEBUG}; ${VALGRIND_FOUND}" OFF)
	if (LIBCAT_WITH_VALGRIND)
		if (NOT VALGRIND_FOUND)
		    message(FATAL_ERROR "Require Valgrind but not found")
		endif()
	    message(STATUS "Build with Valgrind")
	    list(APPEND cat_defines CAT_HAVE_VALGRIND=1)
	    list(APPEND cat_includes ${VALGRIND_INCLUDE_DIR})
	else()
	    message(STATUS "Build without Valgrind")
	endif()
endif()

# llhttp dep
if (1)
    set(llhttp_dir "deps/llhttp")
    add_library(cat_llhttp OBJECT 
        ${llhttp_dir}/src/api.c
        ${llhttp_dir}/src/http.c
        ${llhttp_dir}/src/llhttp.c
    )
    list(APPEND cat_includes ${llhttp_dir}/include)
    target_include_directories(cat_llhttp PRIVATE ${llhttp_dir}/include)
endif()

# OpenSSL dep
find_package(OpenSSL QUIET)
if (NOT OPENSSL_FOUND)
    pkg_check_modules(OPENSSL openssl QUIET)
endif()
cmake_dependent_option(LIBCAT_ENABLE_OPENSSL "Enable OpenSSL if found" ON OPENSSL_FOUND OFF)
if (LIBCAT_ENABLE_OPENSSL)
	if (NOT OPENSSL_FOUND)
	    message(FATAL_ERROR "Require OpenSSL but not found")
	endif()
    message(STATUS "Enable OpenSSL")
    list(APPEND cat_defines CAT_HAVE_OPENSSL=1)
    list(APPEND cat_includes ${OPENSSL_INCLUDE_DIR})
    list(APPEND cat_libraries ${OPENSSL_LIBRARIES})
    list(APPEND cat_sources src/cat_ssl.c)
    if(MSVC)
        list(APPEND cat_libraries crypt32.lib)
    endif()
else()
    message(STATUS "OpenSSL is not enabled")
endif()

# cURL dep
find_package(cURL QUIET)
if (NOT CURL_FOUND)
    pkg_check_modules(CURL libcurl QUIET)
endif()
cmake_dependent_option(LIBCAT_ENABLE_CURL "Enable cURL if found" ON CURL_FOUND OFF)
if (LIBCAT_ENABLE_CURL)
	if (NOT CURL_FOUND)
        message(FATAL_ERROR "Require cURL but not found")
    endif()
    message(STATUS "Enable cURL")
    list(APPEND cat_defines CAT_HAVE_CURL=1)
    list(APPEND cat_includes ${CURL_INCLUDE_DIR})
    list(APPEND cat_libraries ${CURL_LIBRARIES})
    list(APPEND cat_sources src/cat_curl.c)
else()
    message(STATUS "cURL is not enabled")
endif()

add_library(cat STATIC $<TARGET_OBJECTS:cat_context> $<TARGET_OBJECTS:cat_uv> $<TARGET_OBJECTS:cat_llhttp> ${cat_sources})
if(MSVC)
    set_property(TARGET cat PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

target_compile_definitions(cat PRIVATE ${cat_defines})
target_compile_options(cat PRIVATE ${cat_cflags})
# TODO: Separate public and private
target_include_directories(cat PRIVATE ${cat_includes})
target_link_libraries(cat ${cat_libraries})

# Code Coverage Configuration (unix-like)
# TODO: support windows
if(NOT MSVC)
    add_library(coverage_config INTERFACE)
    option(CODE_COVERAGE "Enable coverage reporting" OFF)
    if(CODE_COVERAGE)
        target_compile_options(coverage_config INTERFACE
            -O0
            -g
            --coverage
        )
        if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.13)
            target_link_options(coverage_config INTERFACE --coverage)
        else()
            target_link_libraries(coverage_config INTERFACE --coverage)
        endif()
    endif()
    target_link_libraries(cat coverage_config)
endif()

# tests
find_package(GTest QUIET)
if (NOT GTEST_FOUND)
    pkg_check_modules(CAT_GTEST_MAIN gtest_main QUIET)
    pkg_check_modules(CAT_GTEST gtest QUIET)
else()
    set(CAT_GTEST_MAIN_LIBRARIES "GTest::Main")
    set(CAT_GTEST_LIBRARIES "GTest::GTest")
endif()
cmake_dependent_option(LIBCAT_BUILD_TESTS
    "Build the GTest-based unit tests when we have GTest and we are the root project"
    ON GTEST_FOUND
    OFF)
if (LIBCAT_BUILD_TESTS)
	if (NOT GTEST_FOUND AND NOT (CAT_GTEST_FOUND AND CAT_GTEST_MAIN_FOUND))
	    message(FATAL_ERROR "Require GTest to build tests but not found")
	endif()
    message(STATUS "Enable testing (GTest-based)")
    enable_testing()
    enable_language(CXX)
    if(NOT MSVC)
        set(cat_cxxflags ${cat_cflags} -std=c++11 -Wall)
    else()
        set(cat_cxxflags ${cat_cflags} /std:c++latest /W3 /EHsc)
    endif()
    list(REMOVE_ITEM cat_cxxflags ${lint-strict-prototypes})
    message(STATUS "C++ Compiler is ${CMAKE_CXX_COMPILER_ID}")
	set(cat_test_sources
        tests/test.cc
        tests/test_cat.cc
        tests/test_cat_memory.cc
        tests/test_cat_string.cc
        tests/test_cat_error.cc
        tests/test_cat_log.cc
        tests/test_cat_env.cc
        tests/test_cat_coroutine.cc
        tests/test_cat_event.cc
        tests/test_cat_time.cc
        tests/test_cat_channel.cc
        tests/test_cat_sync.cc
        tests/test_cat_socket.cc
        tests/test_cat_dns.cc
        tests/test_cat_work.cc
        tests/test_cat_buffer.cc
        tests/test_cat_signal.cc
        tests/test_cat_watch_dog.cc
        tests/test_cat_http.cc
    )
    if (LIBCAT_ENABLE_OPENSSL)
        list(APPEND cat_test_sources tests/test_cat_ssl.cc)
    endif()
    if (LIBCAT_ENABLE_CURL)
        list(APPEND cat_test_sources tests/test_cat_curl.cc)
    endif()
    add_executable(cat_tests ${cat_test_sources})
    target_compile_definitions(cat_tests PRIVATE ${cat_defines})
    target_compile_options(cat_tests PRIVATE ${cat_cxxflags})
    target_include_directories(cat_tests PRIVATE /tests/test.h ${cat_includes} ${GTEST_INCLUDE_DIRS})
    # NOTE: gtest may needs gtest_main before its linking (on windows), that's strange
    target_link_libraries(cat_tests PRIVATE cat ${CAT_GTEST_MAIN_LIBRARIES} ${CAT_GTEST_LIBRARIES})
    add_test(
        NAME cat_tests
        COMMAND $<TARGET_FILE:cat_tests> --gtest_color=yes
    )
else()
    message(STATUS "Tests will not be built")
endif()

message(STATUS "summary of build options:
    Install prefix:  ${CMAKE_INSTALL_PREFIX}
    Target system:   ${CMAKE_SYSTEM_NAME}
    Compiler:
      C compiler:    ${CMAKE_C_COMPILER}
      CFLAGS:        ${CMAKE_C_FLAGS_${_build_type}} ${CMAKE_C_FLAGS}
    Build type:      ${CMAKE_BUILD_TYPE}
")
