

#include <stddef.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <sys/stat.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>

#include "random.h"

void *mmap_normal(size_t size) {
    return mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
}

void *mmap_file(size_t size) {
    char filename[PATH_MAX];
    mkdir("./tmp", 0755);
    snprintf(filename, PATH_MAX, "./tmp/cache-%ld", random_next());


    int fd = open(filename, O_RDWR | O_CREAT, 0644);
    if (fd == -1) {
        perror("open");
        exit(1);
    }

    if (ftruncate(fd, size) == -1) {
        perror("ftruncate");
        exit(1);
    }

    void *addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, fd, 0);
    if (addr == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }
    return addr;
}

void madv_lazyfree(void *memory, size_t size) {
    int ret = madvise(memory, size, MADV_FREE);
    assert(ret == 0);
}

void madv_noop(void *memory, size_t size) { }
