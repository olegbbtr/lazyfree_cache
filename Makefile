CC = clang
CFLAGS = -g -Wall -Wextra -fno-omit-frame-pointer -Ofast -march=native -std=gnu11 -Iinclude -Isrc/include
DEV_FLAGS = -fsanitize=address,undefined \
			-fsanitize-address-use-after-scope
# CFLAGS += $(DEV_FLAGS)

SRCS = $(wildcard src/*/*.c)
OBJS = $(patsubst src/%.c,build/%.o,$(SRCS))



all: clean build/test build/benchmark

# run-tests: build/test
# 	./run_tests.sh

build/test: build/test.o $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@

build/benchmark: build/benchmark.o $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@

build/%.o: src/%.c | build build/cache build/util
	$(CC) $(CFLAGS) -c $< -o $@

build build/cache build/util:
	mkdir -p $@

clean:
	rm -rf build
	rm -rf ./tmp


perf: build/benchmark
	sudo perf record -F 999 -g -- ./build/benchmark lazyfree 4 1
	perf report


# all: asan run

# main: src/main.c src/madv_cache.c src/madv_cache.h
# 	clang -g -Wno-unused-result -Wall -Wextra -Werror -o main src/main.c src/madv_cache.c

# asan: src/main.c src/madv_cache.c src/madv_cache.h
# 	clang -g -Wall -Wextra -Werror \
# 		-Wno-unused-result \
# 		-Wno-unused-function \
# 	  	-fno-omit-frame-pointer \
# 		-fsanitize=address,undefined \
# 	  	-fsanitize-address-use-after-scope \
# 	  	-o main src/main.c src/madv_cache.c

# run: main
# 	./main

# run-cgroup: main
	

# clean:
# 	rm -f main

