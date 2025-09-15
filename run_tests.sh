#!/bin/sh

set -e

make

./build/test lazyfree 2
./build/test lazyfree_full 2 

./build/test normal 1
./build/test disk 2

echo "\n===\nAll tests passed"
