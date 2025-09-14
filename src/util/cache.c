

#include <stddef.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <sys/stat.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>

#include "cache.h"

#include "random.h"

void *mmap_normal(size_t size) {
    void *addr = mmap(NULL, size, 
                PROT_READ | PROT_WRITE, 
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE,
                -1, 0);
    if (addr == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    // int ret = madvise(addr, size, MADV_NOHUGEPAGE | MADV_WILLNEED);
    // assert(ret == 0);
    // printf("Anon mmap %zu Mb at %p\n", size / M, addr);
    return addr;
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

    void *addr = mmap(NULL, size, 
        PROT_READ | PROT_WRITE, 
        MAP_SHARED | MAP_NORESERVE, 
        fd, 0);
    if (addr == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }
    close(fd);
    madv_cold(addr, size);
    printf("File mmap %zu Mb at %p\n", size / M, addr);
    return addr;
}

void madv_lazyfree(void *memory, size_t size) {
    int ret;

    ret = madvise(memory, size, MADV_FREE);
    assert(ret == 0);
}

void madv_cold(void *memory, size_t size) {
    printf("MADV_COLD on %zu Mb at %p\n", size / M, memory);
    int ret = madvise(memory, size,  MADV_COLD);
    assert(ret == 0);
}

void madv_noop(void *memory, size_t size) { 
    UNUSED(memory); 
    UNUSED(size); 
}
