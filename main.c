#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include "crc64speed.h"
#include "crc16speed.h"

static long long ustime(void) {
    struct timeval tv;
    long long ust;

    gettimeofday(&tv, NULL);
    ust = ((long long)tv.tv_sec) * 1e6;
    ust += tv.tv_usec;
    return ust;
}

typedef uint64_t (*fns)(uint64_t, void *, uint64_t);

/* GCC 4.8 on Linux is dumb */
#ifndef ftello
extern off_t ftello(FILE *stream);
#endif

static inline uint64_t rdtsc() {
    unsigned int lo = 0, hi = 0;

    /* ask for something that can't be executed out-of-order
     * to force the next rdtsc to not get re-ordered. */
    __sync_synchronize();
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

int main(int argc, char *argv[]) {
    crc64speed_init();
    crc16speed_init();

    if (argc == 1) {
        printf("[64calcu]: e9c6d914c4b8d9ca == %016llx\n",
               (uint64_t)crc64(0, (unsigned char *)"123456789", 9));
        printf("[64looku]: e9c6d914c4b8d9ca == %016llx\n",
               (uint64_t)crc64_lookup(0, (unsigned char *)"123456789", 9));
        printf("[64speed]: e9c6d914c4b8d9ca == %016llx\n",
               (uint64_t)crc64speed(0, (unsigned char *)"123456789", 9));
        printf("[16calcu]: 31c3 == %04llx\n",
               (uint64_t)crc16(0, (unsigned char *)"123456789", 9));
        printf("[16looku]: 31c3 == %04llx\n",
               (uint64_t)crc16_lookup(0, (unsigned char *)"123456789", 9));
        printf("[16speed]: 31c3 == %04llx\n",
               (uint64_t)crc16speed(0, (unsigned char *)"123456789", 9));
        char li[] =
            "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do "
            "eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut "
            "enim ad minim veniam, quis nostrud exercitation ullamco laboris "
            "nisi ut aliquip ex ea commodo consequat. Duis aute irure dolor in "
            "reprehenderit in voluptate velit esse cillum dolore eu fugiat "
            "nulla pariatur. Excepteur sint occaecat cupidatat non proident, "
            "sunt in culpa qui officia deserunt mollit anim id est laborum.";
        printf("[64calcu]: c7794709e69683b3 == %016llx\n",
               (uint64_t)crc64(0, (unsigned char *)li, sizeof li));
        printf("[64looku]: c7794709e69683b3 == %016llx\n",
               (uint64_t)crc64_lookup(0, (unsigned char *)li, sizeof li));
        printf("[64speed]: c7794709e69683b3 == %016llx\n",
               (uint64_t)crc64speed(0, (unsigned char *)li, sizeof li));
        printf("[16calcu]: 4b20 == %04llx\n",
               (uint64_t)crc16(0, (unsigned char *)li, sizeof li));
        printf("[16looku]: 4b20 == %04llx\n",
               (uint64_t)crc16_lookup(0, (unsigned char *)li, sizeof li));
        printf("[16speed]: 4b20 == %04llx\n",
               (uint64_t)crc16speed(0, (unsigned char *)li, sizeof li));

        return 0;
    }

    char *filename = argv[1];
    FILE *fp = fopen(filename, "r");

    if (fseek(fp, 0, SEEK_END) == -1) {
        perror("Can't find file length");
        return 1;
    }
    off_t sz = ftello(fp);
    rewind(fp);
    char *contents = malloc(sz); /* potentially very big */

    if (fread(contents, sz, 1, fp) != 1) {
        free(contents);
        perror("Could not read entire file");
        return 1;
    }
    fclose(fp);

    fns compares[] = { crc64,      crc64_lookup,      crc64speed,
                       (fns)crc16, (fns)crc16_lookup, (fns)crc16speed };
    size_t cc = sizeof(compares) / sizeof(*compares); /* compare count */
    char *names[] = {
        "crc64 (no table)", "crc64 (lookup table)", "crc64speed",
        "crc16 (no table)", "crc16 (lookup table)", "crc16speed"
    };
    uint64_t results[cc];

    double size_mb = sz / 1024.0 / 1024.0;
    printf("Comparing CRCs against %0.2lf MB file...\n\n", size_mb);

    bool error = false;
    uint64_t accum_result = 0;
    for (size_t i = 0; i < cc; i++) {
        /* prime the code path with a dummy untimed call */
        compares[i](0, (unsigned char *)"howdy", 5);

        long long start = ustime();
        uint64_t start_c = rdtsc();
        unsigned long long result =
            compares[i](0, (unsigned char *)contents, sz);
        uint64_t stop_c = rdtsc();
        long long end = ustime();

        results[i] = result;

        double total_time_seconds = (end - start) / 1e6;
        double speed = size_mb / total_time_seconds; /* MB per second */
        double cycles = (double)(stop_c - start_c) / sz;

        if (argc > 2) { /* easier parsing for comparisons */
            printf("%016llx:%lf\n", result, speed);
        } else { /* boring human readable results */
            printf("%s\n", names[i]);
            printf("CRC = %016llx\n", result);
            printf("%lf seconds at %0.2f MB/s (%0.2f CPU cycles per byte)\n",
                   total_time_seconds, speed, cycles);
        }

        /* We test outputs in pairs, so compare every 3 results for equality. */
        if (i % 3 == 0) {
            accum_result = result;
        } else if (accum_result != result) {
            printf("ERROR: CRC results don't match! (%016llx vs. %016llx)\n",
                   accum_result, result);
            error = true;
        }

        printf("\n");
        fflush(stdout);
    }
    free(contents);

    return error;
}
