#!/bin/sh

set -e

./build/test lazyfree 1G smoke
./build/test lazyfree 1G check_twice
# ./build/test random 1G smoke
# ./build/test random 1G check_twice
