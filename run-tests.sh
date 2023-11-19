#!/bin/bash
__DIR__=$(cd "$(dirname "$0")" || exit 1; pwd); [ -z "${__DIR__}" ] && exit 1

set -e
  # go to workspace
  cd "${__DIR__}"
  mkdir -p build
  cd build

  # solve definitions
  if [ "${LIBCAT_BUILD_TYPE-Debug}"x == "Debug"x ]; then
    LIBCAT_ENABLE_ASAN_DEFAULT_VALUE="ON"
  fi
  cmake \
    -DCMAKE_BUILD_TYPE="${LIBCAT_BUILD_TYPE-Debug}" \
    -DLIBCAT_ENABLE_DEBUG_LOG="${LIBCAT_ENABLE_DEBUG_LOG-ON}" \
    -DLIBCAT_USE_THREAD_CONTEXT="${LIBCAT_USE_THREAD_CONTEXT-OFF}" \
    -DLIBCAT_ENABLE_ASAN="${LIBCAT_ENABLE_ASAN-${LIBCAT_ENABLE_ASAN_DEFAULT_VALUE}}" \
    -DLIBCAT_WITH_VALGRIND="${LIBCAT_WITH_VALGRIND-ON}" \
    -DLIBCAT_USE_THREAD_LOCAL="${LIBCAT_USE_THREAD_LOCAL-OFF}" \
    -DLIBCAT_USE_THREAD_KEY="${LIBCAT_USE_THREAD_KEY-OFF}" \
    -DLIBCAT_ENABLE_OPENSSL="${LIBCAT_ENABLE_OPENSSL-ON}" \
    -DLIBCAT_ENABLE_CURL="${LIBCAT_ENABLE_CURL-ON}" \
    -DLIBCAT_ENABLE_POSTGRESQL="${LIBCAT_ENABLE_POSTGRESQL-ON}" \
    ..

  # build tests
  cmake --build . --target cat_tests -- -j"$(${__DIR__}/tools/cpu_count.sh)"
set +e

# run tests
exec ./cat_tests "$@"
