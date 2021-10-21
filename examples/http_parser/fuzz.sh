#!/bin/sh

[ ! -f ./http_parser ] && { echo "build http parser first" >&2 && exit 1; }

afl-fuzz -i fuzz_cases -o fuzz_results -- ./http_parser "$@"
