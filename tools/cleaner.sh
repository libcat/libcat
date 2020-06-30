#!/bin/bash
__DIR__=$(cd "$(dirname "$0")" || exit 1; pwd); [ -z "${__DIR__}" ] && exit 1

error(){ echo "[ERROR] $1"; exit 1; }
success(){ echo "[SUCCESS] $1"; exit 0; }
info(){ echo "[INFO] $1";}

workdir="$1"

if ! cd "${workdir}"; then
  error "Cd to ${workdir} failed"
fi

info "Scanning dir \"${workdir}\" ..."

if [ ! -f "./Makefile" ] && [ ! -f "./CMakeLists.txt" ]; then
  error "Non-project dir ${workdir}"
fi

if [ ! -f "./CMakeLists.txt" ]; then
  USE_AUTOTOOLS=1
elif [ -z "${USE_AUTOTOOLS}" ]; then
  USE_AUTOTOOLS=0
fi

info "USE_AUTOTOOLS=${USE_AUTOTOOLS}"

if [ "${USE_AUTOTOOLS}" != "1" ] && [ -f "./build/CMakeCache.txt" ]; then
  info "CMake build dir will be removed:"
  rm -rf -v ./build
fi

info "Following files will be removed:"

if [ "${USE_AUTOTOOLS}" = "1" ]; then
  find . \( -name \*.gcno -o -name \*.gcda -follow -path build \) -print0 | xargs -0 rm -f -v
  find . \( -name \*.lo -o -name \*.o      -follow -path build \) -print0 | xargs -0 rm -f -v
  find . \( -name \*.la -o -name \*.a      -follow -path build \) -print0 | xargs -0 rm -f -v
  find . \( -name \*.so                    -follow -path build \) -print0 | xargs -0 rm -f -v
  find . \( -name .libs -a -type d         -follow -path build \) -print0 | xargs -0 rm -rf -v
else
  find . \( -name main -a -type f          -follow             \) -print0 | xargs -0 rm -f -v
fi

success "Clean '${workdir}' done"
