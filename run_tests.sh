#!/bin/sh

set -e

./build/test lazyfree 1
./build/test lazyfree_full 1 

echo "All tests passed"
