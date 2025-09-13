#!/bin/sh

set -e

docker build . -t madv_cache

docker run -t --rm --memory 2G madv_cache