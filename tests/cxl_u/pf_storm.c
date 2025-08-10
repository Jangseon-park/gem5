// Simple page-fault generator: allocate a large buffer and touch one byte per page.
#define _GNU_SOURCE
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <unistd.h>

static void *xmalloc_or_mmap(size_t size) {
    // Prefer anonymous mmap to avoid overcommit surprises
    void *p = mmap(NULL, size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p != MAP_FAILED) return p;
    // Fallback to malloc
    p = malloc(size);
    return p;
}

int main(int argc, char **argv) {
    size_t mb = 512; // default 512MB
    if (argc >= 2) {
        char *end = NULL;
        long long v = strtoll(argv[1], &end, 10);
        if (end && *end == '\0' && v > 0) mb = (size_t)v;
    }

    const long page = sysconf(_SC_PAGESIZE);
    size_t bytes = mb * 1024ULL * 1024ULL;
    if (bytes == 0) {
        fprintf(stderr, "Invalid size\n");
        return 1;
    }

    fprintf(stdout, "pf_storm: allocating %zu MB (%zu bytes), page=%ld\n",
            mb, bytes, page);
    fflush(stdout);

    void *buf = xmalloc_or_mmap(bytes);
    if (!buf || buf == MAP_FAILED) {
        perror("alloc");
        return 1;
    }

    // Touch one byte per page to force first-touch page faults
    volatile unsigned char *p = (volatile unsigned char *)buf;
    size_t touched = 0;
    for (size_t off = 0; off < bytes; off += (size_t)page) {
        p[off] = (unsigned char)(off);
        touched++;
    }
    fprintf(stdout, "pf_storm: touched %zu pages\n", touched);
    fflush(stdout);

    // Read back a stride to generate loads too
    size_t sum = 0;
    for (size_t off = 0; off < bytes; off += (size_t)page * 4) {
        sum += p[off];
    }
    fprintf(stdout, "pf_storm: sum=%zu\n", sum);
    fflush(stdout);

    // Keep buffer resident a bit
    if (buf && buf != MAP_FAILED) {
        // Try to keep mappings alive until exit
    }
    return 0;
}


