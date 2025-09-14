#!/bin/sh

set -e

make

./build/test lazyfree 1
./build/test lazyfree_full 1 

./build/test normal 1
./build/test disk 1

echo "\n===\nAll tests passed"
