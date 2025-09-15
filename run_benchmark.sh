#!/bin/sh
set -e

docker build . -t lazyfree_cache

run_test() {
    echo "== Benchmark: $1, set_size=$2 cache_size=$3, soft_limit=$3 hard_limit=$4 GB =="
    CMD="/app/build/benchmark '$1' '$2' '$3'"
    # CMD="gdb -ex run --args $CMD"
    docker run  -it --rm \
                --memory-reservation "$3.5"G \
                --memory-swap "$4"G \
                --memory "$4"G \
                --oom-score-adj=-900 \
                lazyfree_cache /bin/sh -c "$CMD"
                # --oom-kill-disable \
                # --memory-swappiness=0 \
}
# run_test lazyfree 4 4 5
# sleep 5
# run_test disk     4 4 5
# sleep 5
run_test anon   4 2 5 # Can only run with 1Gb cache
