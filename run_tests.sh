#!/bin/sh

set -e

make build/test

./build/test lazyfree 2
./build/test lazyfree_full 2 

./build/test anon 1
./build/test disk 2

echo "\n===\nAll tests passed"
