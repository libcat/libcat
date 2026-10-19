/*
  +--------------------------------------------------------------------------+
  | libcat                                                                   |
  +--------------------------------------------------------------------------+
  | Licensed under the Apache License, Version 2.0 (the "License");          |
  | you may not use this file except in compliance with the License.         |
  | You may obtain a copy of the License at                                  |
  | http://www.apache.org/licenses/LICENSE-2.0                               |
  +--------------------------------------------------------------------------+
  | Author: Yun Dou <dixyes@gmail.com>                                       |
  +--------------------------------------------------------------------------+
 */

/*
 * Ethash, the proof of work Ethereum used before it moved to proof of stake,
 * run over the threads of this library.
 *
 * This is not a miner and it is not meant to be fast. It exists to load the
 * threads with a shape that matrix multiplication does not: a large read only
 * table shared by every thread, reached at random, and a search that stops as
 * soon as one thread reports a result.
 *
 * Only the light path is here, which computes the dataset items it needs from
 * the cache instead of materializing the full dataset. The result is the same
 * as the full path, and the cache fits in 16 MiB.
 *
 * The Keccak calls go to OpenSSL, which exposes the original Keccak as
 * KECCAK-256 and KECCAK-512, not the padded SHA3 variants.
 *
 * The seven cases below come from the reference implementation
 * (chfast/ethash, test/unittests/test_cases.hpp), so the algorithm can be
 * checked against the protocol and not only against itself.
 *
 * On 16 cores, one thread of this build reaches about 430 hashes per second and
 * sixteen reach about 6900. The reference implementation reaches about 520 on
 * one thread of the same machine with its own Keccak. The rest of the gap is the
 * per call dispatch of the OpenSSL interface. This example accepts that gap,
 * because it exists to load the threads and not to be a fast hash.
 *
 * Usage:
 *   ethash --verify          check the algorithm against the seven cases
 *   ethash [seconds] [threads]
 *                            hash for that many seconds, ten by default
 */

#include "cat.h"
#include "cat_api.h"
#include "cat_thread.h"
#include "cat_thread_sync.h"

#include <openssl/evp.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ------------------------------------------------------------------------- */
/* Protocol constants                                                         */
/* ------------------------------------------------------------------------- */

#define ETHASH_EPOCH_LENGTH 30000
#define ETHASH_DATASET_ACCESSES 64
#define ETHASH_MAX_EPOCH 32639
#define ETHASH_CACHE_INIT_SIZE (1u << 24)
#define ETHASH_CACHE_GROWTH (1u << 17)
#define ETHASH_CACHE_ROUNDS 3
#define ETHASH_DATASET_INIT_SIZE (1u << 30)
#define ETHASH_DATASET_GROWTH (1u << 23)
#define ETHASH_DATASET_PARENTS 256

/* A cache item is a 512 bit hash, a dataset item is two of them, and the mix
 * keeps one dataset item worth of words. */
#define ETHASH_HASH_WORDS 16
#define ETHASH_ITEM_WORDS 32
#define ETHASH_MIX_WORDS 32

union ethash_hash256 {
    uint32_t word32s[8];
    uint8_t bytes[32];
};

union ethash_hash512 {
    uint32_t word32s[ETHASH_HASH_WORDS];
    uint64_t word64s[8];
    uint8_t bytes[64];
};

union ethash_hash1024 {
    uint32_t word32s[ETHASH_ITEM_WORDS];
    uint64_t word64s[16];
    uint8_t bytes[128];
};

struct ethash_result_s {
    union ethash_hash256 final_hash;
    union ethash_hash256 mix_hash;
};

struct ethash_cache_s {
    /* the read only table every thread reaches into */
    union ethash_hash512 *items;
    uint32_t item_count;
    uint32_t dataset_count;
};

/* ------------------------------------------------------------------------- */
/* Keccak through OpenSSL                                                     */
/* ------------------------------------------------------------------------- */

static EVP_MD *ethash_keccak_256;
static EVP_MD *ethash_keccak_512;
static _Thread_local EVP_MD_CTX *ethash_keccak_ctx;

static int ethash_keccak_init(void)
{
    ethash_keccak_256 = EVP_MD_fetch(NULL, "KECCAK-256", NULL);
    ethash_keccak_512 = EVP_MD_fetch(NULL, "KECCAK-512", NULL);

    return ethash_keccak_256 != NULL && ethash_keccak_512 != NULL;
}

static void ethash_keccak(const void *data, size_t size, void *out, int bits)
{
    EVP_MD_CTX *ctx = ethash_keccak_ctx;
    unsigned int length = 0;

    if (ctx == NULL) {
        ctx = EVP_MD_CTX_new();
        ethash_keccak_ctx = ctx;
    }

    (void) EVP_DigestInit_ex(ctx, bits == 256 ? ethash_keccak_256 : ethash_keccak_512, NULL);
    (void) EVP_DigestUpdate(ctx, data, size);
    (void) EVP_DigestFinal_ex(ctx, (unsigned char *) out, &length);
}

static void ethash_keccak256(const void *data, size_t size, union ethash_hash256 *out)
{
    ethash_keccak(data, size, out->bytes, 256);
}

static void ethash_keccak512(const void *data, size_t size, union ethash_hash512 *out)
{
    ethash_keccak(data, size, out->bytes, 512);
}

/* ------------------------------------------------------------------------- */
/* Small helpers                                                              */
/* ------------------------------------------------------------------------- */

/* Ethash is defined for little endian bytes, and the words below are read out
 * of byte strings, so a big endian host has to swap. */
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define ETHASH_BIG_ENDIAN 1
#endif

static uint32_t ethash_le32(const uint8_t *bytes)
{
    uint32_t value;

    memcpy(&value, bytes, sizeof(value));
#ifdef ETHASH_BIG_ENDIAN
    value = __builtin_bswap32(value);
#endif

    return value;
}

/* The core of FNV-1, the mixing function the protocol uses everywhere. */
static uint32_t ethash_fnv1(uint32_t left, uint32_t right)
{
    return (left * 0x01000193u) ^ right;
}

static void ethash_fnv1_hash512(union ethash_hash512 *left, const union ethash_hash512 *right)
{
    for (int index = 0; index < ETHASH_HASH_WORDS; index++) {
        left->word32s[index] = ethash_fnv1(left->word32s[index], right->word32s[index]);
    }
}

static void ethash_xor_hash512(union ethash_hash512 *out, const union ethash_hash512 *left,
                               const union ethash_hash512 *right)
{
    for (int index = 0; index < 8; index++) {
        out->word64s[index] = left->word64s[index] ^ right->word64s[index];
    }
}

static int ethash_is_odd_prime(int number)
{
    for (int divisor = 3; (int64_t) divisor * (int64_t) divisor <= (int64_t) number; divisor += 2) {
        if (number % divisor == 0) {
            return 0;
        }
    }

    return 1;
}

/* The protocol sizes the cache and the dataset to a prime number of items. */
static int ethash_largest_prime(int upper_bound)
{
    int number = upper_bound;

    if (number < 2) {
        return 0;
    }
    if (number == 2) {
        return 2;
    }
    if (number % 2 == 0) {
        number--;
    }
    while (!ethash_is_odd_prime(number)) {
        number -= 2;
    }

    return number;
}

static int ethash_epoch_of(unsigned int block_number)
{
    return (int) (block_number / ETHASH_EPOCH_LENGTH);
}

static int ethash_cache_item_count(int epoch)
{
    int upper = (int) (ETHASH_CACHE_INIT_SIZE / sizeof(union ethash_hash512)) +
                epoch * (int) (ETHASH_CACHE_GROWTH / sizeof(union ethash_hash512));

    return ethash_largest_prime(upper);
}

static int ethash_dataset_item_count(int epoch)
{
    int upper = (int) (ETHASH_DATASET_INIT_SIZE / sizeof(union ethash_hash1024)) +
                epoch * (int) (ETHASH_DATASET_GROWTH / sizeof(union ethash_hash1024));

    return ethash_largest_prime(upper);
}

/* ------------------------------------------------------------------------- */
/* Cache, dataset items and the hash itself                                   */
/* ------------------------------------------------------------------------- */

static void ethash_seed(int epoch, union ethash_hash256 *seed)
{
    memset(seed, 0, sizeof(*seed));

    for (int round = 0; round < epoch; round++) {
        ethash_keccak256(seed->bytes, sizeof(*seed), seed);
    }
}

static void ethash_build_cache(struct ethash_cache_s *cache, int epoch)
{
    union ethash_hash256 seed;
    union ethash_hash512 item;
    union ethash_hash512 mixed;
    uint32_t count;

    ethash_seed(epoch, &seed);

    cache->item_count = (uint32_t) ethash_cache_item_count(epoch);
    cache->dataset_count = (uint32_t) ethash_dataset_item_count(epoch);
    count = cache->item_count;

    ethash_keccak512(seed.bytes, sizeof(seed), &item);
    cache->items[0] = item;

    for (uint32_t index = 1; index < count; index++) {
        ethash_keccak512(item.bytes, sizeof(item), &item);
        cache->items[index] = item;
    }

    for (int round = 0; round < ETHASH_CACHE_ROUNDS; round++) {
        for (uint32_t index = 0; index < count; index++) {
            /* the first word of the item, read as a little endian number, picks
             * the item to mix in */
            uint32_t first = ethash_le32(cache->items[index].bytes) % count;
            uint32_t previous = (uint32_t) ((count + index - 1) % count);

            ethash_xor_hash512(&mixed, &cache->items[first], &cache->items[previous]);
            ethash_keccak512(mixed.bytes, sizeof(mixed), &cache->items[index]);
        }
    }
}

/* One dataset item, computed from the cache instead of stored, which is what
 * makes the light path possible. */
static void ethash_dataset_item(const struct ethash_cache_s *cache, uint32_t index,
                                 union ethash_hash1024 *out)
{
    union ethash_hash512 mix0;
    union ethash_hash512 mix1;
    uint32_t count = cache->item_count;
    uint32_t seed0 = index * 2;
    uint32_t seed1 = seed0 + 1;

    mix0 = cache->items[seed0 % count];
    mix1 = cache->items[seed1 % count];

    mix0.word32s[0] ^= seed0;
    mix1.word32s[0] ^= seed1;

    ethash_keccak512(mix0.bytes, sizeof(mix0), &mix0);
    ethash_keccak512(mix1.bytes, sizeof(mix1), &mix1);

    for (uint32_t parent = 0; parent < ETHASH_DATASET_PARENTS; parent++) {
        uint32_t word0 = mix0.word32s[parent % ETHASH_HASH_WORDS];
        uint32_t word1 = mix1.word32s[parent % ETHASH_HASH_WORDS];
        uint32_t from0 = ethash_fnv1(seed0 ^ parent, word0) % count;
        uint32_t from1 = ethash_fnv1(seed1 ^ parent, word1) % count;

        ethash_fnv1_hash512(&mix0, &cache->items[from0]);
        ethash_fnv1_hash512(&mix1, &cache->items[from1]);
    }

    ethash_keccak512(mix0.bytes, sizeof(mix0), (union ethash_hash512 *) &out->word32s[0]);
    ethash_keccak512(mix1.bytes, sizeof(mix1), (union ethash_hash512 *) &out->word32s[ETHASH_HASH_WORDS]);
}

static void ethash_hash(const struct ethash_cache_s *cache, const union ethash_hash256 *header,
                        uint64_t nonce, struct ethash_result_s *result)
{
    union ethash_hash512 seed;
    union ethash_hash1024 mix;
    union ethash_hash1024 item;
    uint8_t input[sizeof(*header) + sizeof(nonce)];
    uint8_t final_input[sizeof(seed) + sizeof(result->mix_hash)];

    memcpy(input, header->bytes, sizeof(*header));
    memcpy(input + sizeof(*header), &nonce, sizeof(nonce));
    ethash_keccak512(input, sizeof(input), &seed);

    for (int index = 0; index < ETHASH_HASH_WORDS; index++) {
        mix.word32s[index] = seed.word32s[index];
        mix.word32s[index + ETHASH_HASH_WORDS] = seed.word32s[index];
    }

    for (int round = 0; round < ETHASH_DATASET_ACCESSES; round++) {
        uint32_t position = ethash_fnv1((uint32_t) round ^ seed.word32s[0],
                                        mix.word32s[round % ETHASH_MIX_WORDS]) % cache->dataset_count;

        ethash_dataset_item(cache, position, &item);

        for (int word = 0; word < ETHASH_ITEM_WORDS; word++) {
            mix.word32s[word] = ethash_fnv1(mix.word32s[word], item.word32s[word]);
        }
    }

    for (int index = 0; index < ETHASH_MIX_WORDS; index += 4) {
        uint32_t mixed = ethash_fnv1(mix.word32s[index], mix.word32s[index + 1]);

        mixed = ethash_fnv1(mixed, mix.word32s[index + 2]);
        mixed = ethash_fnv1(mixed, mix.word32s[index + 3]);
        result->mix_hash.word32s[index / 4] = mixed;
    }

    memcpy(final_input, seed.bytes, sizeof(seed));
    memcpy(final_input + sizeof(seed), result->mix_hash.bytes, sizeof(result->mix_hash));
    ethash_keccak256(final_input, sizeof(final_input), &result->final_hash);
}

/* ------------------------------------------------------------------------- */
/* The reference cases                                                        */
/* ------------------------------------------------------------------------- */

struct ethash_case_s {
    unsigned int block_number;
    const char *header_hash;
    const char *nonce;
    const char *mix_hash;
    const char *final_hash;
};

static const struct ethash_case_s ethash_cases[] = {
    { 0,
      "2a8de2adf89af77358250bf908bf04ba94a6e8c3ba87775564a41d269a05e4ce", "4242424242424242",
      "58f759ede17a706c93f13030328bcea40c1d1341fb26f2facd21ceb0dae57017",
      "dd47fd2d98db51078356852d7c4014e6a5d6c387c35f40e2875b74a256ed7906" },
    { 2,
      "100cbec5e5ef82991290d0d93d758f19082e71f234cf479192a8b94df6da6bfe", "307692cf71b12f6d",
      "e55d02c555a7969361cf74a9ec6211d8c14e4517930a00442f171bdb1698d175",
      "ab9b13423cface72cbec8424221651bc2e384ef0f7a560e038fc68c8d8684829" },
    { 2683077,
      "0313d03c5ed78694c90ecb3d04190b82d5b222c75ba4cab83383dde4d11ed512", "8c5eaec000788d41",
      "93e85c97b34ccd8091e09ddb513fdc9e680fa8898d4a0737205e60af710a3dcb",
      "00000000000204882a6213f68fe89bc368df25c1ad999f82532a7433e99bc48e" },
    { 5000000,
      "bc544c2baba832600013bd5d1983f592e9557d04b0fb5ef7a100434a5fc8d52a", "4617a20003ba3f25",
      "94cd4e844619ee20989578276a0a9046877d569d37ba076bf2e8e34f76189dea",
      "0000000000001a5b18ae31b7417b7c045bc6dd78cc3b694c4bebfaefef6b3c56" },
    { 5000001,
      "2cd14041cfc3bd13064cfd58e26c0bddf1e97a4202c4b8076444a7cd4515f8c3", "1af47f2007922384",
      "46cb1268ac6b218eb01b9bd46f1533561d085620c9bfc23eadb8ab106f84b2d8",
      "0000000000000a4230cfc483a3f739a5101192c3d4f9c8314f4508be24c28257" },
    { 5000002,
      "9e79bced19062baf7c47e516ad3a1bd779222404d05b4205def30a13c7d87b5b", "c9a044201dd998f2",
      "028a19d6dcf0975972285f71a23fe3a468886ad24cc63f969158d427556d6bb5",
      "0000000000000eec80fbc4c472507b6c212c4f216043318f3fb3e696a0e80b06" },
    { 5306861,
      "53a005f209a4dc013f022a5078c6b38ced76e767a30367ff64725f23ec652a9f", "d337f82001e992c5",
      "26c64f063dac85ae3585526b446be6754faf044876aa3e20f770bea567e04d1d",
      "00000000000003f1554d8071ff0903268fcb70f30f4af3bf7ec7dc69cdf509f3" },
};

static int ethash_unhex(const char *text, uint8_t *out, size_t size)
{
    for (size_t index = 0; index < size; index++) {
        unsigned int byte = 0;

        if (sscanf(text + index * 2, "%2x", &byte) != 1) {
            return 0;
        }
        out[index] = (uint8_t) byte;
    }

    return 1;
}

static int ethash_case_run(const struct ethash_cache_s *cache_ready, int epoch,
                           const struct ethash_case_s *test, int log)
{
    struct ethash_result_s result;
    union ethash_hash256 header;
    union ethash_hash256 expected_mix;
    union ethash_hash256 expected_final;
    uint64_t nonce;
    int ok = 1;

    if (!ethash_unhex(test->header_hash, header.bytes, sizeof(header)) ||
        !ethash_unhex(test->mix_hash, expected_mix.bytes, sizeof(expected_mix)) ||
        !ethash_unhex(test->final_hash, expected_final.bytes, sizeof(expected_final))) {
        fprintf(stderr, "case %u: bad hex\n", test->block_number);
        return 0;
    }
    /* the nonce in the case is a number written in hex, not a byte string, so
     * it goes into the input in the byte order of this host */
    nonce = strtoull(test->nonce, NULL, 16);

    ethash_hash(cache_ready, &header, nonce, &result);

    if (memcmp(result.mix_hash.bytes, expected_mix.bytes, sizeof(expected_mix)) != 0) {
        ok = 0;
    }
    if (memcmp(result.final_hash.bytes, expected_final.bytes, sizeof(expected_final)) != 0) {
        ok = 0;
    }

    if (log) {
        printf("case block %7u epoch %3d cache %7u items: %s\n", test->block_number, epoch,
            cache_ready->item_count, ok ? "ok" : "MISMATCH");
        if (!ok) {
            printf("  mix   got ");
            for (size_t index = 0; index < sizeof(expected_mix); index++) {
                printf("%02x", result.mix_hash.bytes[index]);
            }
            printf("\n  mix   want ");
            for (size_t index = 0; index < sizeof(expected_mix); index++) {
                printf("%02x", expected_mix.bytes[index]);
            }
            printf("\n");
        }
    }

    return ok;
}

/* ------------------------------------------------------------------------- */
/* The threaded run                                                           */
/* ------------------------------------------------------------------------- */

struct ethash_job_s {
    cat_thread_t thread;
    const struct ethash_cache_s *cache;
    const union ethash_hash256 *header;
    union ethash_hash256 checked_mix;
    union ethash_hash256 checked_final;
    cat_atomic_bool_t *stop;
    uint64_t start_nonce;
    uint64_t hashes;
    int verified;
};

static void ethash_routine(cat_data_t *data)
{
    struct ethash_job_s *job = (struct ethash_job_s *) data;
    struct ethash_result_s result;
    uint64_t nonce = job->start_nonce;

    while (!cat_atomic_bool_load(job->stop)) {
        /* a batch, so the flag is read once in a while instead of per hash */
        for (int round = 0; round < 4; round++) {
            ethash_hash(job->cache, job->header, nonce, &result);
            nonce++;
            job->hashes++;
        }
    }

    /*
     * Every thread reads the same read only cache, so a result that differs
     * from the one the single threaded pass produced would mean the cache is
     * not really read only.
     */
    ethash_hash(job->cache, job->header, 0x4242424242424242ull, &result);
    job->checked_mix = result.mix_hash;
    job->checked_final = result.final_hash;
    job->verified = 1;
}

static double ethash_now(void)
{
    struct timespec now;

    (void) clock_gettime(CLOCK_MONOTONIC, &now);

    return (double) now.tv_sec + (double) now.tv_nsec / 1e9;
}

int main(int argc, char **argv)
{
    static cat_thread_t main_thread;
    int verify_only = argc > 1 && strcmp(argv[1], "--verify") == 0;
    int seconds = argc > 1 && !verify_only ? atoi(argv[1]) : 10;
    long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    int threads = argc > 2 ? atoi(argv[2]) : (cpus > 0 ? (int) cpus : 1);
    struct ethash_cache_s cache = {};
    struct ethash_job_s *jobs = NULL;
    union ethash_hash256 header;
    union ethash_hash256 expected_mix;
    union ethash_hash256 expected_final;
    cat_atomic_bool_t stop;
    uint64_t total = 0;
    double started;
    int exit_code = 0;
    int created = 0;

    if (!ethash_keccak_init()) {
        fprintf(stderr, "openssl has no KECCAK-256/KECCAK-512 digest\n");
        return 1;
    }

    /* The algorithm first, before any thread is involved. The cases are ordered
     * by block, so an epoch is built once and reused by the cases that share it. */
    {
        struct ethash_cache_s case_cache = {};
        int built_epoch = -1;

        for (size_t index = 0; index < sizeof(ethash_cases) / sizeof(ethash_cases[0]); index++) {
            int epoch = ethash_epoch_of(ethash_cases[index].block_number);

            if (epoch != built_epoch) {
                cat_sys_free(case_cache.items);
                case_cache.items = (union ethash_hash512 *) cat_sys_malloc(
                    (size_t) ethash_cache_item_count(epoch) * sizeof(union ethash_hash512));
                if (case_cache.items == NULL) {
                    fprintf(stderr, "out of memory for epoch %d\n", epoch);
                    return 1;
                }
                ethash_build_cache(&case_cache, epoch);
                built_epoch = epoch;
            }

            if (!ethash_case_run(&case_cache, epoch, &ethash_cases[index], 1)) {
                exit_code = 1;
            }
        }

        cat_sys_free(case_cache.items);
    }
    if (exit_code != 0) {
        printf("ethash: the algorithm does not match the protocol, so the run stops here\n");
        return exit_code;
    }
    if (verify_only) {
        return 0;
    }

    if (seconds <= 0 || threads <= 0) {
        fprintf(stderr, "usage: %s [--verify] [seconds] [threads]\n", argv[0]);
        return 2;
    }

    if (!cat_init_all() || !cat_thread_register_main(&main_thread)) {
        fprintf(stderr, "runtime init failed: %s\n", cat_get_last_error_message());
        return 1;
    }
    if (!cat_run(CAT_RUN_EASY)) {
        fprintf(stderr, "run failed: %s\n", cat_get_last_error_message());
        return 1;
    }

    /* the cache of epoch 0 is the one every case above used, and it is what the
     * threads below read */
    cache.items = (union ethash_hash512 *) cat_sys_malloc(
        (size_t) ethash_cache_item_count(0) * sizeof(union ethash_hash512));
    jobs = (struct ethash_job_s *) cat_sys_malloc((size_t) threads * sizeof(*jobs));
    if (cache.items == NULL || jobs == NULL) {
        fprintf(stderr, "out of memory\n");
        exit_code = 1;
        goto _exit;
    }

    started = ethash_now();
    ethash_build_cache(&cache, 0);
    printf("ethash: epoch 0 cache of %u items built in %.3f s (%.1f MiB read only)\n",
        cache.item_count, ethash_now() - started,
        (double) cache.item_count * sizeof(union ethash_hash512) / (1024.0 * 1024.0));
    fflush(stdout);

    memset(&header, 0, sizeof(header));
    memset(jobs, 0, (size_t) threads * sizeof(*jobs));
    if (!ethash_unhex(ethash_cases[0].header_hash, header.bytes, sizeof(header)) ||
        !ethash_unhex(ethash_cases[0].mix_hash, expected_mix.bytes, sizeof(expected_mix)) ||
        !ethash_unhex(ethash_cases[0].final_hash, expected_final.bytes, sizeof(expected_final))) {
        exit_code = 1;
        goto _exit;
    }

    cat_atomic_bool_init(&stop, cat_false);

    printf("ethash: %d threads hashing for %d s\n", threads, seconds);
    fflush(stdout);

    started = ethash_now();

    for (int index = 0; index < threads; index++) {
        jobs[index].cache = &cache;
        jobs[index].header = &header;
        jobs[index].stop = &stop;
        jobs[index].start_nonce = (uint64_t) index << 32;

        if (!cat_thread_create(&jobs[index].thread, ethash_routine, &jobs[index])) {
            fprintf(stderr, "thread %d failed: %s\n", index, cat_get_last_error_message());
            exit_code = 1;
            break;
        }
        created++;
    }

    if (created > 0) {
        /* the deadline lives here, and the workers only poll the flag */
        while (ethash_now() - started < (double) seconds) {
            (void) cat_time_delay(20);
        }
        cat_atomic_bool_store(&stop, cat_true);

        for (int index = 0; index < created; index++) {
            if (!cat_thread_join(&jobs[index].thread, CAT_TIMEOUT_FOREVER)) {
                fprintf(stderr, "join %d failed: %s\n", index, cat_get_last_error_message());
                exit_code = 1;
            }
        }
    }

    {
        double elapsed = ethash_now() - started;
        int verified = 0;

        for (int index = 0; index < created; index++) {
            total += jobs[index].hashes;
            if (jobs[index].verified &&
                memcmp(jobs[index].checked_mix.bytes, expected_mix.bytes, sizeof(expected_mix)) == 0 &&
                memcmp(jobs[index].checked_final.bytes, expected_final.bytes, sizeof(expected_final)) == 0) {
                verified++;
            }
        }

        printf("ethash: %llu hashes in %.3f s, %.1f hash/s\n",
            (unsigned long long) total, elapsed, (double) total / elapsed);
        printf("ethash: %d of %d threads reproduced the reference hash after the run (%s)\n",
            verified, created, verified == created ? "ok" : "MISMATCH");
        if (verified != created) {
            exit_code = 1;
        }
    }

_exit:
    cat_sys_free(jobs);
    cat_sys_free(cache.items);

    (void) cat_stop();
    (void) cat_runtime_shutdown_all();
    (void) cat_runtime_close_all();

    return exit_code;
}
