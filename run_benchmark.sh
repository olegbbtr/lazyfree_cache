#!/bin/sh
set -e

docker build . -t lazyfree_cache

run_test() {
    echo "== Docker test: $1, cache_size=$2 factor=$3, soft_limit=$4 hard_limit=$5 GB =="
    CMD="/app/build/benchmark '$1' '$2' '$3'"
    # CMD="gdb -ex run --args $CMD"
    docker run  -it --rm \
                --memory-reservation "$4"G \
                --memory-swap "$5"G \
                --memory "$5"G \
                --oom-score-adj=-900 \
                lazyfree_cache /bin/sh -c "$CMD"


                # --oom-kill-disable \
                # --memory-swappiness=0 \
}
run_test lazyfree 4 1 4.25 4.5
sleep 5
run_test disk     4 1 4.25 4.5
sleep 5
run_test normal   4 0.25 4.25 4.5


# run_test normal 2 2.2 3 
# run_test disk 2 2.2 3
