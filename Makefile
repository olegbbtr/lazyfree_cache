CC = clang
CFLAGS = -g -Wall -Wextra -fno-omit-frame-pointer -O3 -march=native -std=gnu18 -Iinclude -Isrc/include
CFLAGS_DEV_EXTRA = -fsanitize=address,undefined \
	   			   -fsanitize-address-use-after-scope
CFLAGS += $(CFLAGS_DEV_EXTRA)

SRCS = $(wildcard src/*/*.c)
OBJS = $(patsubst src/%.c,build/%.o,$(SRCS))

.PHONY: all build-all run clean
all: clean build-all

build-all: build/test build/benchmark

clean:
	rm -rf build
	rm -rf ./tmp
	test -f perf.data && sudo rm -rf perf.data		   || true
	test -f perf.data.old && sudo rm -rf perf.data.old || true

run: 
	./run_tests.sh
	./run_benchmark.sh

build/test: build/test.o $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@

build/benchmark: build/benchmark.o $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@

build/%.o: src/%.c | build build/cache build/util
	$(CC) $(CFLAGS) -c $< -o $@

build build/cache build/util:
	mkdir -p $@

perf: build/benchmark
	sudo perf record -F 999 -g -b -- ./build/benchmark lazyfree 4 1
	sudo perf report
