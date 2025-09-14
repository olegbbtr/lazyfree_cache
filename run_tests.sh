#!/bin/sh

set -e

./build/test lazyfree 1 smoke
./build/test lazyfree 1 check_twice
./build/test lazyfree 1 check_twicex2
# ./build/test random 1G smoke
# ./build/test random 1G check_twice
