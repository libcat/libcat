#!/bin/bash -e

ulimit -n 65536
ab -c 1024 -n 1000000 -k http://127.0.0.1:9764/
