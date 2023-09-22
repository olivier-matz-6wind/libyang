/**
 * @file hash_table_internal.h
 * @author Radek Krejci <rkrejci@cesnet.cz>
 * @author Michal Vasko <mvasko@cesnet.cz>
 * @brief libyang hash table internal header
 *
 * Copyright (c) 2015 - 2023 CESNET, z.s.p.o.
 *
 * This source code is licensed under BSD 3-Clause License (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://opensource.org/licenses/BSD-3-Clause
 */

#ifndef LY_HASH_TABLE_INTERNAL_H_
#define LY_HASH_TABLE_INTERNAL_H_

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

#include "compat.h"
#include "hash_table.h"

/** reference value for 100% */
#define LYHT_HUNDRED_PERCENTAGE 100

/** when the table is at least this much percent full, it is enlarged (double the size) */
#define LYHT_ENLARGE_PERCENTAGE 75

/** only once the table is this much percent full, enable shrinking */
#define LYHT_FIRST_SHRINK_PERCENTAGE 50

/** when the table is less than this much percent full, it is shrunk (half the size) */
#define LYHT_SHRINK_PERCENTAGE 25

/** never shrink beyond this size */
#define LYHT_MIN_SIZE 8

/**
 * @brief Generic hash table record.
 */
struct ly_ht_rec {
    uint32_t hash;        /* hash of the value */
    uint32_t next;        /* index of next collision */
    unsigned char val[]; /* arbitrary-size value */
};

struct ly_ht_bucket {
    uint32_t first;
    uint32_t last;
};

/**
 * @brief (Very) generic hash table.
 *
 * The hash table is composed of a table of indexes that references the
 * first record. The records contain a next index that references the
 * next record in case of collision.
 * The free records are chained starting from first_free_rec.
 */
struct ly_ht {
    uint32_t used;        /* number of values stored in the hash table (filled records) */
    uint32_t size;        /* always holds 2^x == size (is power of 2), actually number of records allocated */
    lyht_value_equal_cb val_equal; /* callback for testing value equivalence */
    void *cb_data;        /* user data callback arbitrary value */
    uint16_t resize;      /* 0 - resizing is disabled, *
                           * 1 - enlarging is enabled, *
                           * 2 - both shrinking and enlarging is enabled */
    uint16_t rec_size;    /* real size (in bytes) of one record for accessing recs array */
    uint32_t first_free_rec; /* index of the first free record */
    struct ly_ht_bucket *buckets; /* pointer to the buckets table */
    unsigned char *recs;  /* pointer to the hash table itself (array of struct ht_rec) */
};

/* index that points to nothing */
#define LYHT_NO_RECORD UINT32_MAX

/* get the effective size of the record, after alignment */
static inline uint32_t
lyht_align_rec_size(uint32_t rec_size)
{
    return (rec_size + 7) & ~7;
}

/* get the record associated to */
static inline struct ly_ht_rec *
lyht_get_rec(unsigned char *recs, uint16_t rec_size, uint32_t idx)
{
    return (struct ly_ht_rec *)&recs[idx * lyht_align_rec_size(rec_size)];
}

/* Iterate all records in a bucket */
#define LYHT_ITER_BUCKET_RECS(ht, bucket_idx, rec_idx, rec)             \
    for (rec_idx = ht->buckets[bucket_idx].first,                       \
             rec = lyht_get_rec(ht->recs, ht->rec_size, rec_idx);       \
         rec_idx != LYHT_NO_RECORD;                                     \
         rec_idx = rec->next,                                           \
             rec = lyht_get_rec(ht->recs, ht->rec_size, rec_idx))

/* Iterate all records in the hash table */
#define LYHT_ITER_ALL_RECS(ht, bucket_idx, rec_idx, rec)                \
    for (bucket_idx = 0; bucket_idx < ht->size; bucket_idx++)           \
        LYHT_ITER_BUCKET_RECS(ht, bucket_idx, rec_idx, rec)

/**
 * @brief Dictionary hash table record.
 */
struct ly_dict_rec {
    char *value;        /**< stored string */
    uint32_t refcount;  /**< reference count of the string */
};

/**
 * @brief Dictionary for storing repeated strings.
 */
struct ly_dict {
    struct ly_ht *hash_tab;
    pthread_mutex_t lock;
};

/**
 * @brief Initiate content (non-zero values) of the dictionary
 *
 * @param[in] dict Dictionary table to initiate
 */
void lydict_init(struct ly_dict *dict);

/**
 * @brief Cleanup the dictionary content
 *
 * @param[in] dict Dictionary table to cleanup
 */
void lydict_clean(struct ly_dict *dict);

#endif /* LY_HASH_TABLE_INTERNAL_H_ */
