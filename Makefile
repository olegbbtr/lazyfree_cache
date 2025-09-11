all: run

main: main.c madv_free_cache.c madv_free_cache.h
	clang -g -Wno-unused-result -Wall -Wextra -Werror -o main main.c madv_free_cache.c

asan: main.c madv_free_cache.c madv_free_cache.h
	clang -g -O1 -fno-omit-frame-pointer \
	  -fsanitize=address,undefined \
	  -fsanitize-address-use-after-scope \
	  -o main main.c madv_free_cache.c

run: main
	./main

clean:
	rm -f main

