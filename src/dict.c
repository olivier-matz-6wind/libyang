/**
 * @file dict.c
 * @author Radek Krejci <rkrejci@cesnet.cz>
 * @author Michal Vasko <mvasko@cesnet.cz>
 * @brief libyang dictionary for storing strings
 *
 * Copyright (c) 2015 - 2023 CESNET, z.s.p.o.
 *
 * This source code is licensed under BSD 3-Clause License (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://opensource.org/licenses/BSD-3-Clause
 */

#include "dict.h"

#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "compat.h"
#include "log.h"

/* starting size of the dictionary */
#define LYDICT_MIN_SIZE 1024

/**
 * @brief Comparison callback for dictionary's hash table
 *
 * Implementation of ::lyht_value_equal_cb.
 */
static ly_bool
lydict_val_eq(void *val1_p, void *val2_p, ly_bool UNUSED(mod), void * UNUSED(cb_data))
{
    LY_CHECK_ARG_RET(NULL, val1_p, val2_p, 0);

    const char *str1 = ((struct ly_dict_rec *)val1_p)->value;
    const char *str2 = ((struct ly_dict_rec *)val2_p)->value;

    LY_CHECK_ERR_RET(!str1, LOGARG(NULL, val1_p), 0);
    LY_CHECK_ERR_RET(!str2, LOGARG(NULL, val2_p), 0);

    if (strcmp(str1, str2) == 0) {
        return 1;
    }

    return 0;
}

void
lydict_init(struct ly_dict *dict)
{
    LY_CHECK_ARG_RET(NULL, dict, );

    dict->hash_tab = lyht_new(LYDICT_MIN_SIZE, sizeof(struct ly_dict_rec), lydict_val_eq, NULL, 1);
    LY_CHECK_ERR_RET(!dict->hash_tab, LOGINT(NULL), );
    pthread_mutex_init(&dict->lock, NULL);
}

void
lydict_clean(struct ly_dict *dict)
{
    struct ly_dict_rec *dict_rec = NULL;
    struct ly_ht_rec *rec = NULL;

    LY_CHECK_ARG_RET(NULL, dict, );

    for (uint32_t i = 0; i < dict->hash_tab->size; i++) {
        /* get ith record */
        rec = (struct ly_ht_rec *)&dict->hash_tab->recs[i * dict->hash_tab->rec_size];
        if (rec->hits == 1) {
            /*
             * this should not happen, all records inserted into
             * dictionary are supposed to be removed using lydict_remove()
             * before calling lydict_clean()
             */
            dict_rec = (struct ly_dict_rec *)rec->val;
            LOGWRN(NULL, "String \"%s\" not freed from the dictionary, refcount %d", dict_rec->value, dict_rec->refcount);
            /* if record wasn't removed before free string allocated for that record */
#ifdef NDEBUG
            free(dict_rec->value);
#endif
        }
    }

    /* free table and destroy mutex */
    lyht_free(dict->hash_tab, NULL);
    pthread_mutex_destroy(&dict->lock);
}

static ly_bool
lydict_resize_val_eq(void *val1_p, void *val2_p, ly_bool mod, void *cb_data)
{
    LY_CHECK_ARG_RET(NULL, val1_p, val2_p, 0);

    const char *str1 = ((struct ly_dict_rec *)val1_p)->value;
    const char *str2 = ((struct ly_dict_rec *)val2_p)->value;

    LY_CHECK_ERR_RET(!str1, LOGARG(NULL, val1_p), 0);
    LY_CHECK_ERR_RET(!str2, LOGARG(NULL, val2_p), 0);

    if (mod) {
        /* used when inserting new values */
        if (strcmp(str1, str2) == 0) {
            return 1;
        }
    } else {
        /* used when finding the original value again in the resized table */
        return lydict_val_eq(val1_p, val2_p, mod, cb_data);
    }

    return 0;
}

LIBYANG_API_DEF LY_ERR
lydict_remove(const struct ly_ctx *ctx, const char *value)
{
    LY_ERR ret = LY_SUCCESS;
    size_t len;
    uint32_t hash;
    struct ly_dict_rec rec, *match = NULL;
    char *val_p;

    if (!ctx || !value) {
        return LY_SUCCESS;
    }

    LOGDBG(LY_LDGDICT, "removing \"%s\"", value);

    len = strlen(value);
    hash = lyht_hash(value, len);

    /* create record for lyht_find call */
    rec.value = (char *)value;
    rec.refcount = 0;

    pthread_mutex_lock((pthread_mutex_t *)&ctx->dict.lock);
    /* check if value is already inserted */
    ret = lyht_find(ctx->dict.hash_tab, &rec, hash, (void **)&match);

    if (ret == LY_SUCCESS) {
        LY_CHECK_ERR_GOTO(!match, LOGINT(ctx), finish);

        /* if value is already in dictionary, decrement reference counter */
        match->refcount--;
        if (match->refcount == 0) {
            /*
             * remove record
             * save pointer to stored string before lyht_remove to
             * free it after it is removed from hash table
             */
            val_p = match->value;
            ret = lyht_remove_with_resize_cb(ctx->dict.hash_tab, &rec, hash, lydict_resize_val_eq);
            free(val_p);
            LY_CHECK_ERR_GOTO(ret, LOGINT(ctx), finish);
        }
    } else if (ret == LY_ENOTFOUND) {
        LOGERR(ctx, LY_ENOTFOUND, "Value \"%s\" was not found in the dictionary.", value);
    } else {
        LOGINT(ctx);
    }

finish:
    pthread_mutex_unlock((pthread_mutex_t *)&ctx->dict.lock);
    return ret;
}

LY_ERR
dict_insert(const struct ly_ctx *ctx, char *value, size_t len, const char **str_p)
{
    LY_ERR ret = LY_SUCCESS;
    struct ly_dict_rec *match = NULL, rec;
    uint32_t hash;

    LOGDBG(LY_LDGDICT, "inserting \"%.*s\"", (int)len, value);

    hash = lyht_hash(value, len);
    /* create record for lyht_insert */
    rec.value = value;
    rec.refcount = 1;

    ret = lyht_insert_with_resize_cb(ctx->dict.hash_tab, (void *)&rec, hash, lydict_resize_val_eq, (void **)&match);
    if (ret == LY_EEXIST) {
        match->refcount++;
        free(value);
        ret = LY_SUCCESS;
    } else if (ret != LY_SUCCESS) {
        /* lyht_insert returned error */
        free(value);
        return ret;
    }

    if (str_p) {
        *str_p = match->value;
    }

    return ret;
}

LIBYANG_API_DEF LY_ERR
lydict_insert(const struct ly_ctx *ctx, const char *value, size_t len, const char **str_p)
{
    char *value_copy;
    LY_ERR result;

    LY_CHECK_ARG_RET(ctx, ctx, str_p, LY_EINVAL);

    if (!value) {
        *str_p = NULL;
        return LY_SUCCESS;
    }

    if (!len) {
        len = strlen(value);
    }

    value_copy = malloc(len + 1);
    LY_CHECK_ERR_RET(!value_copy, LOGMEM(ctx), LY_EMEM);

    memcpy(value_copy, value, len);
    value_copy[len] = '\0';

    pthread_mutex_lock((pthread_mutex_t *)&ctx->dict.lock);
    result = dict_insert(ctx, value_copy, len, str_p);
    pthread_mutex_unlock((pthread_mutex_t *)&ctx->dict.lock);

    return result;
}

LIBYANG_API_DEF LY_ERR
lydict_insert_zc(const struct ly_ctx *ctx, char *value, const char **str_p)
{
    LY_ERR result;

    LY_CHECK_ARG_RET(ctx, ctx, str_p, LY_EINVAL);

    if (!value) {
        *str_p = NULL;
        return LY_SUCCESS;
    }

    pthread_mutex_lock((pthread_mutex_t *)&ctx->dict.lock);
    result = dict_insert(ctx, value, strlen(value), str_p);
    pthread_mutex_unlock((pthread_mutex_t *)&ctx->dict.lock);

    return result;
}
