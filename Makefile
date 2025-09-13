CC = clang
CFLAGS = -g -Wall -Wextra -O0 -std=gnu11 -Iinclude -Isrc/include

CACHE_SRCS = $(wildcard src/cache/*.c)
CACHE_OBJS = $(patsubst src/cache/%.c,build/cache/%.o,$(CACHE_SRCS))

all: test

test: build/test
	./build/test lazyfree 4

build/test: build/test.o $(CACHE_OBJS)
	$(CC) $^ -o $@

build/%.o: src/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/cache/%.o: src/cache/%.c | build/cache
	$(CC) $(CFLAGS) -c $< -o $@

build build/cache:
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

