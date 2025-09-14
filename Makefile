CC = clang
CFLAGS = -g -Wall -Wextra -O0 -std=gnu11 -Iinclude -Isrc/include

SRCS = $(wildcard src/*/*.c)
OBJS = $(patsubst src/%.c,build/%.o,$(SRCS))



all: run-tests

run-tests: clean build/test
	./run_tests.sh

build/test: build/test.o $(OBJS)
	$(CC) $^ -o $@

build/%.o: src/%.c | build build/cache build/util
	$(CC) $(CFLAGS) -c $< -o $@

build build/cache build/util:
	mkdir -p $@

clean:
	rm -rf build



# CFLAGS_ASAN := $(CFLAGS) \
#  				-fno-omit-frame-pointer \
#  				-fsanitize=address,undefined \
#  				-fsanitize-address-use-after-scope
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

