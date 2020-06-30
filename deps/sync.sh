#!/bin/bash
__DIR__=$(cd "$(dirname "$0")" || exit 1; pwd); [ -z "${__DIR__}" ] && exit 1

BOOST_CONTEXT_VERSION="develop"
LIBUV_VERSION="develop"
LLHTTP_VERSION="v2.0.5"

sync(){ "${__DIR__}/../tools/dm.sh" "${__DIR__}/../" "$@"; }

# ====== boost-context ======

context_dir="${__DIR__}/context/asm"

sync \
"boost-context" \
"https://github.com/boostorg/context/archive/${BOOST_CONTEXT_VERSION}.tar.gz" \
"context-${BOOST_CONTEXT_VERSION}/src/asm" \
"${__DIR__}/context/asm"

if [ -d "${context_dir}" ] && cd "${context_dir}"; then
  ALL_FILES=()
  while IFS='' read -r line; do ALL_FILES+=("$line"); done < <(ls ./*.asm ./*.S)

  perl -p -i -e 's;\bjump_fcontext\b;cat_coroutine_context_jump;g' "${ALL_FILES[@]}";
  perl -p -i -e 's;\b_jump_fcontext\b;_cat_coroutine_context_jump;g' "${ALL_FILES[@]}";
  perl -p -i -e 's;\b_make_fcontext\b;_cat_coroutine_context_make;g' "${ALL_FILES[@]}";
  perl -p -i -e 's;\bmake_fcontext\b;cat_coroutine_context_make;g' "${ALL_FILES[@]}";
  perl -p -i -e 's;\b_ontop_fcontext\b;_cat_coroutine_context_ontop;g' "${ALL_FILES[@]}";
  perl -p -i -e 's;\bontop_fcontext\b;cat_coroutine_context_ontop;g' "${ALL_FILES[@]}";

  perl -p -i -e 's;\bBOOST_CONTEXT_EXPORT\b;EXPORT;g' "${ALL_FILES[@]}";
  perl -p -i -e 's;\bBOOST_USE_TSX\b;CAT_USE_TSX;g' "${ALL_FILES[@]}";

  find . \( -name \*.cpp \) -print0 | xargs -0 rm -f
fi

# ====== libuv ======

sync \
"libuv" \
"https://github.com/libcat/libuv/archive/${LIBUV_VERSION}.tar.gz" \
"libuv-${LIBUV_VERSION}" \
"${__DIR__}/libuv"

# ====== llhttp ======

sync \
"llhttp" \
"https://github.com/nodejs/llhttp/archive/release/${LLHTTP_VERSION}.tar.gz" \
"llhttp-release-${LLHTTP_VERSION}" \
"${__DIR__}/llhttp"
