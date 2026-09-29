// SPDX-License-Identifier: GPL-2.0
/*
 * memflip: look for DRAM bit flips from userspace (B8000-H stability work).
 *
 * Kernel Oopses on kernel #20/#21 show single flipped bits in otherwise
 * intact kernel text (e.g. 0xf8d5 read as 0xf0d5).  This fills a large
 * buffer per thread with address-derived patterns, re-reads it in a loop
 * and reports every mismatch with the bits that differ, so a DRAM /
 * refresh / voltage problem shows up without any driver involved.
 *
 * Usage: memflip [MiB per thread] [threads] [seconds] [hold]  (default 64 4 600 0)
 * hold > 0: retention mode - fill once (all-ones / all-zeros halves), then
 * verify every <hold> seconds without rewriting, to catch cells that lose
 * their charge between refreshes (1->0 or 0->1 in data left untouched).
 * Build: NDK clang --target=armv7a-linux-androideabi19 -O2 -fPIE -pie (dynamic:
 * static bionic from NDK r25c rejects the TLS alignment on this old linker)
 */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

static size_t words;
static double seconds;
static unsigned int hold;
static volatile unsigned long total_errors;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static double now(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* Pattern for word i in pass p: mixes address bits and pass number. */
static uint32_t pattern(size_t i, unsigned int p)
{
	uint32_t x = (uint32_t)i * 2654435761u ^ p * 0x9e3779b9u;

	switch (p & 3) {
	case 0: return x;
	case 1: return ~x;
	case 2: return (p & 4) ? 0xffffffffu : 0;
	default: return 1u << (i & 31);
	}
}

static void *worker(void *arg)
{
	int id = (int)(intptr_t)arg;
	uint32_t *buf = mmap(NULL, words * 4, PROT_READ | PROT_WRITE,
			     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	double t0 = now();
	unsigned int pass;
	size_t i;

	if (buf == MAP_FAILED) {
		perror("mmap");
		return NULL;
	}
	if (hold) {
		/* First half ones (true cells), second half zeros (anti cells). */
		for (i = 0; i < words; i++)
			buf[i] = i < words / 2 ? 0xffffffffu : 0;
		for (pass = 0; now() - t0 < seconds; pass++) {
			sleep(hold);
			for (i = 0; i < words; i++) {
				uint32_t want = i < words / 2 ? 0xffffffffu : 0;
				uint32_t got = buf[i];

				if (got == want)
					continue;
				pthread_mutex_lock(&lock);
				total_errors++;
				if (total_errors <= 200)
					printf("HOLD-ERR t%d after %.0f s va %p want %08x "
					       "got %08x xor %08x\n", id, now() - t0,
					       (void *)&buf[i], want, got, want ^ got);
				pthread_mutex_unlock(&lock);
				buf[i] = want;
			}
			if (id == 0)
				printf("hold check %u, %.0f s, errors %lu\n", pass + 1,
				       now() - t0, total_errors);
		}
		munmap(buf, words * 4);
		return NULL;
	}
	for (pass = 0; now() - t0 < seconds; pass++) {
		for (i = 0; i < words; i++)
			buf[i] = pattern(i, pass);
		/* Read back twice: a flip that shows up only once is a read glitch. */
		for (int r = 0; r < 2; r++) {
			for (i = 0; i < words; i++) {
				uint32_t want = pattern(i, pass), got = buf[i];

				if (got == want)
					continue;
				pthread_mutex_lock(&lock);
				total_errors++;
				if (total_errors <= 200)
					printf("ERR t%d pass %u read %d va %p want %08x got %08x "
					       "xor %08x reread %08x\n", id, pass, r,
					       (void *)&buf[i], want, got, want ^ got,
					       buf[i]);
				pthread_mutex_unlock(&lock);
				buf[i] = want;
			}
		}
		if (id == 0 && pass % 4 == 3)
			printf("pass %u, %.0f s, errors %lu\n", pass + 1, now() - t0,
			       total_errors);
	}
	munmap(buf, words * 4);
	return NULL;
}

int main(int argc, char **argv)
{
	size_t mib = argc > 1 ? strtoul(argv[1], NULL, 0) : 64;
	int threads = argc > 2 ? atoi(argv[2]) : 4, t;
	pthread_t tid[16];

	seconds = argc > 3 ? atof(argv[3]) : 600;
	hold = argc > 4 ? (unsigned int)atoi(argv[4]) : 0;
	words = mib << 18;
	if (threads < 1 || threads > 16)
		return 2;
	setvbuf(stdout, NULL, _IOLBF, 0);
	printf("memflip: %d threads x %zu MiB, %.0f s, hold %u s\n", threads, mib,
	       seconds, hold);
	for (t = 0; t < threads; t++)
		pthread_create(&tid[t], NULL, worker, (void *)(intptr_t)t);
	for (t = 0; t < threads; t++)
		pthread_join(tid[t], NULL);
	printf("memflip done: %lu errors\n", total_errors);
	return total_errors ? 1 : 0;
}
