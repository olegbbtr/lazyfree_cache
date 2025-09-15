#!/bin/sh
set -e

docker build . -t lazyfree_cache

run_test() {
    echo "== Benchmark: $1, capacity_bytes=$2 Gb reclaim_bytes=$3 Gb soft_limit=$4 Gb hard_limit=$5 Gb =="
    CMD="/app/build/benchmark $1 $2 $3"
    # CMD="gdb -ex run --args $CMD"
    docker run  -it --rm \
                --memory-reservation "$4"G \
                --memory-swap        "$5"G \
                --memory             "$5"G \
                --oom-score-adj=-900        \
                lazyfree_cache /bin/sh -c "$CMD"

                # --oom-kill-disable \
                # --memory-swappiness=0 \
}
#        impl     capacity  reclaim soft_limit  hard_limit
run_test lazyfree 4         3       4.25         4.5
run_test disk     4         3       4.25         4.5
run_test anon     1         3       4.25         4.5
run_test stub     4         3       4.25         4.5

echo "\n===\nAll benchmarks passed"
