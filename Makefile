all: asan run

main: src/main.c src/madv_cache.c src/madv_cache.h
	clang -g -Wno-unused-result -Wall -Wextra -Werror -o main src/main.c src/madv_cache.c

asan: src/main.c src/madv_cache.c src/madv_cache.h
	clang -g -Wall -Wextra -Werror \
		-Wno-unused-result \
		-Wno-unused-function \
	  	-fno-omit-frame-pointer \
		-fsanitize=address,undefined \
	  	-fsanitize-address-use-after-scope \
	  	-o main src/main.c src/madv_cache.c

run: main
	./main

run-cgroup: main
	

clean:
	rm -f main

