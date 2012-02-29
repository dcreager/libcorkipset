/* -*- coding: utf-8 -*-
 * ----------------------------------------------------------------------
 * Copyright © 2010-2012, RedJack, LLC.
 * All rights reserved.
 *
 * Please see the LICENSE.txt file in this distribution for license
 * details.
 * ----------------------------------------------------------------------
 */

#include <assert.h>
#include <string.h>

#include <libcork/core.h>
#include <libcork/ds.h>

#include "ipset/bdd/nodes.h"
#include "ipset/logging.h"
#include "internal.h"


static void
ipset_trinary_key_init(struct ipset_trinary_key *key,
                       ipset_node_id f, ipset_node_id g, ipset_node_id h)
{
    key->f = f;
    key->g = g;
    key->h = h;
}


// forward declaration

static ipset_node_id
cached_ite(struct ipset_node_cache *cache,
           ipset_node_id f, ipset_node_id g, ipset_node_id h);


/**
 * Perform an actual trinary operation.
 */

static ipset_node_id
apply_ite(struct ipset_node_cache *cache,
          ipset_node_id f, ipset_node_id g, ipset_node_id h)
{
    /* We know this isn't a trivial case, since otherwise it wouldn't
     * been picked up in cached_ite(), so we need to recurse. */

    assert(ipset_node_get_type(f) == IPSET_NONTERMINAL_NODE);

    struct ipset_node  *f_node =
        ipset_node_cache_get_nonterminal(cache, f);
    struct ipset_node  *g_node = NULL;
    struct ipset_node  *h_node = NULL;

    /* There's at least one nonterminal node.  We need the lowest
     * nonterminal variable index. */

    ipset_variable  min_variable = f_node->variable;

    if (ipset_node_get_type(g) == IPSET_NONTERMINAL_NODE) {
        g_node = ipset_node_cache_get_nonterminal(cache, g);
        if (g_node->variable < min_variable) {
            min_variable = g_node->variable;
        }
    }

    if (ipset_node_get_type(h) == IPSET_NONTERMINAL_NODE) {
        h_node = ipset_node_cache_get_nonterminal(cache, h);
        if (h_node->variable < min_variable) {
            min_variable = h_node->variable;
        }
    }

    /* We're going to do two recursive calls, a “low” one and a “high”
     * one.  For each nonterminal that has the minimum variable number,
     * we use its low and high pointers in the respective recursive
     * call.  For all other nonterminals, and for all terminals, we use
     * the operand itself. */

    ipset_node_id  low_f, high_f;
    ipset_node_id  low_g, high_g;
    ipset_node_id  low_h, high_h;

    /* we know that F is nonterminal */
    if (f_node->variable == min_variable) {
        low_f = f_node->low;
        high_f = f_node->high;
    } else {
        low_f = f;
        high_f = f;
    }

    if ((ipset_node_get_type(g) == IPSET_NONTERMINAL_NODE) &&
        (g_node->variable == min_variable)) {
        low_g = g_node->low;
        high_g = g_node->high;
    } else {
        low_g = g;
        high_g = g;
    }

    if ((ipset_node_get_type(h) == IPSET_NONTERMINAL_NODE) &&
        (h_node->variable == min_variable)) {
        low_h = h_node->low;
        high_h = h_node->high;
    } else {
        low_h = h;
        high_h = h;
    }

    /* Perform the recursion. */
    ipset_node_id low_result =
        cached_ite(cache, low_f, low_g, low_h);
    ipset_node_id high_result =
        cached_ite(cache, high_f, high_g, high_h);

    return ipset_node_cache_nonterminal
        (cache, min_variable, low_result, high_result);
}


/**
 * Perform an actual trinary operation, checking the cache first.
 */

static ipset_node_id
cached_ite(struct ipset_node_cache *cache,
           ipset_node_id f, ipset_node_id g, ipset_node_id h)
{
    DEBUG("Applying ITE(%u,%u,%u)", f, g, h);

    /* Some trivial cases first. */

    /* If F is a terminal, then we're in one of the following two
     * cases:
     *
     *   ITE(1,G,H) = G
     *   ITE(0,G,H) = H
     */

    if (ipset_node_get_type(f) == IPSET_TERMINAL_NODE) {
        ipset_value  f_value = ipset_terminal_value(f);
        ipset_node_id  result = (f_value == 0)? h: g;
        DEBUG("Trivial result = %u", result);
        return result;
    }

    /* ITE(F,G,G) == G */
    if (g == h) {
        DEBUG("Trivial result = %u", g);
        return g;
    }

    /* ITE(F,1,0) = F */
    if ((ipset_node_get_type(g) == IPSET_TERMINAL_NODE) &&
        (ipset_node_get_type(h) == IPSET_TERMINAL_NODE)) {
        ipset_value  g_value = ipset_terminal_value(g);
        ipset_value  h_value = ipset_terminal_value(h);

        if ((g_value == 1) && (h_value == 0)) {
            DEBUG("Trivial result = %u", f);
            return f;
        }
    }

    /* Check to see if we've already performed the operation on these
     * operands. */

    struct cork_hash_table_entry  *entry;
    bool  is_new;
    struct ipset_trinary_key  search_key;
    ipset_trinary_key_init(&search_key, f, g, h);

    entry = cork_hash_table_get_or_create
        (&cache->ite_cache, &search_key, &is_new);

    if (!is_new) {
        /* There's a result in the cache, so return it. */
        DEBUG("Existing result = %u", (ipset_node_id) (uintptr_t) entry->value);
        return (ipset_node_id) (uintptr_t) entry->value;
    } else {
        /* This result doesn't exist yet.  Allocate a permanent copy of
         * the key.  Apply the operator, add the result to the cache,
         * and then return it. */

        struct ipset_trinary_key  *real_key =
            cork_new(struct ipset_trinary_key);
        ipset_node_id  result;
        memcpy(real_key, &search_key, sizeof(struct ipset_trinary_key));
        entry->key = real_key;
        result = apply_ite(cache, f, g, h);
        entry->value = (void *) (uintptr_t) result;
        DEBUG("NEW result = %u", result);
        return result;
    }
}


ipset_node_id
ipset_node_cache_ite(struct ipset_node_cache *cache,
                     ipset_node_id f, ipset_node_id g, ipset_node_id h)
{
    return cached_ite(cache, f, g, h);
}
