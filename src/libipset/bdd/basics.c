/* -*- coding: utf-8 -*-
 * ----------------------------------------------------------------------
 * Copyright © 2010-2012, RedJack, LLC.
 * All rights reserved.
 *
 * Please see the LICENSE.txt file in this distribution for license
 * details.
 * ----------------------------------------------------------------------
 */

#include <stdio.h>
#include <string.h>

#include <libcork/core.h>

#include "ipset/bdd/nodes.h"
#include "ipset/bits.h"
#include "ipset/logging.h"
#include "internal.h"


void
ipset_node_fprint(FILE *stream, struct ipset_node *node)
{
    fprintf(stream, "nonterminal(x%u? %u: %u)",
            node->variable, node->high, node->low);
}


static cork_hash
ipset_node_hasher(const void *key)
{
    const struct ipset_node  *node = key;
    /* Hash of "ipset_node" */
    cork_hash  hash = 0xf3b7dc44;
    hash = cork_hash_variable(hash, node->variable);
    hash = cork_hash_variable(hash, node->low);
    hash = cork_hash_variable(hash, node->high);
    return hash;
}

static bool
ipset_node_comparator(const void *key1, const void *key2)
{
    const struct ipset_node  *node1 = key1;
    const struct ipset_node  *node2 = key2;

    if (node1 == node2) {
        return true;
    }

    return
        (node1->variable == node2->variable) &&
        (node1->low == node2->low) &&
        (node1->high == node2->high);
}


static cork_hash
ipset_binary_key_hasher(const void *vkey)
{
    const struct ipset_binary_key  *key = vkey;
    /* Hash of "ipset_binary_key" */
    cork_hash  hash = 0xe7b61538;
    hash = cork_hash_variable(hash, key->lhs);
    hash = cork_hash_variable(hash, key->rhs);
    return hash;
}

static bool
ipset_binary_key_comparator(const void *vkey1, const void *vkey2)
{
    const struct ipset_binary_key  *key1 = vkey1;
    const struct ipset_binary_key  *key2 = vkey2;

    if (key1 == key2) {
        return true;
    }

    return (key1->lhs == key2->lhs) && (key1->rhs == key2->rhs);
}


static cork_hash
ipset_trinary_key_hasher(const void *vkey)
{
    const struct ipset_trinary_key  *key = vkey;
    /* Hash of "ipset_trinary_key" */
    cork_hash  hash = 0xf21216b3;
    hash = cork_hash_variable(hash, key->f);
    hash = cork_hash_variable(hash, key->g);
    hash = cork_hash_variable(hash, key->h);
    return hash;
}

static bool
ipset_trinary_key_comparator(const void *vkey1, const void *vkey2)
{
    const struct ipset_trinary_key  *key1 = vkey1;
    const struct ipset_trinary_key  *key2 = vkey2;

    if (key1 == key2) {
        return true;
    }

    return
        (key1->f == key2->f) &&
        (key1->g == key2->g) &&
        (key1->h == key2->h);
}


struct ipset_node_cache *
ipset_node_cache_new()
{
    struct ipset_node_cache  *cache = cork_new(struct ipset_node_cache);
    cork_array_init(&cache->chunks);
    cache->largest_index = 0;
    cork_hash_table_init
        (&cache->node_cache, 0, ipset_node_hasher, ipset_node_comparator);
    cork_hash_table_init
        (&cache->and_cache, 0,
         ipset_binary_key_hasher, ipset_binary_key_comparator);
    cork_hash_table_init
        (&cache->or_cache, 0,
         ipset_binary_key_hasher, ipset_binary_key_comparator);
    cork_hash_table_init
        (&cache->ite_cache, 0,
         ipset_trinary_key_hasher, ipset_trinary_key_comparator);
    return cache;
}


static enum cork_hash_table_map_result
free_keys(struct cork_hash_table_entry *entry, void *ud)
{
    free(entry->key);
    return CORK_HASH_TABLE_MAP_DELETE;
}


void
ipset_node_cache_free(struct ipset_node_cache *cache)
{
    size_t  i;
    for (i = 0; i < cork_array_size(&cache->chunks); i++) {
        free(cork_array_at(&cache->chunks, i));
    }
    cork_array_done(&cache->chunks);
    cork_hash_table_done(&cache->node_cache);
    cork_hash_table_map(&cache->and_cache, free_keys, NULL);
    cork_hash_table_done(&cache->and_cache);
    cork_hash_table_map(&cache->or_cache, free_keys, NULL);
    cork_hash_table_done(&cache->or_cache);
    cork_hash_table_map(&cache->ite_cache, free_keys, NULL);
    cork_hash_table_done(&cache->ite_cache);
    free(cache);
}


/**
 * Returns the index of a new ipset_node instance.
 */
static ipset_value
ipset_node_cache_alloc_node(struct ipset_node_cache *cache)
{
    ipset_value  next_index = cache->largest_index++;
    ipset_value  chunk_index = next_index >> IPSET_BDD_NODE_CACHE_BIT_SIZE;
    if (chunk_index >= cork_array_size(&cache->chunks)) {
        /* We've filled up all of the existing chunks, and need to
         * create a new one. */
        DEBUG("Allocating chunk #%zu", cork_array_size(&cache->chunks));
        struct ipset_node  *new_chunk =
            cork_calloc(IPSET_BDD_NODE_CACHE_SIZE, sizeof(struct ipset_node));
        cork_array_append(&cache->chunks, new_chunk);
    }
    return next_index;
}

ipset_node_id
ipset_node_cache_nonterminal(struct ipset_node_cache *cache,
                             ipset_variable variable,
                             ipset_node_id low, ipset_node_id high)
{
    /* Don't allow any nonterminals whose low and high subtrees are the
     * same, since the nonterminal would be redundant. */
    if (CORK_UNLIKELY(low == high)) {
        DEBUG("Skipping nonterminal(x%u? %u: %u)", variable, high, low);
        return low;
    }

    /* Check to see if there's already a nonterminal with these contents
     * in the cache. */
    DEBUG("Searching for nonterminal(x%u? %u: %u)", variable, high, low);

    struct ipset_node  search_node;
    search_node.variable = variable;
    search_node.low = low;
    search_node.high = high;

    bool  is_new;
    struct cork_hash_table_entry  *entry =
        cork_hash_table_get_or_create
        (&cache->node_cache, &search_node, &is_new);

    if (!is_new) {
        /* There's already a node with these contents, so return its ID. */
        struct ipset_node  *node = entry->key;
        DEBUG("Existing node, ID = %u", node->id);
        return node->id;
    } else {
        /* This node doesn't exist yet.  Allocate a permanent copy of
         * the node, add it to the cache, and then return its ID. */
        ipset_value  new_index = ipset_node_cache_alloc_node(cache);
        ipset_node_id  new_node_id = ipset_nonterminal_node_id(new_index);
        struct ipset_node  *real_node =
            ipset_node_cache_get_nonterminal_by_index(cache, new_index);
        real_node->id = new_node_id;
        real_node->variable = variable;
        real_node->low = low;
        real_node->high = high;
        entry->key = real_node;
        DEBUG("NEW node, ID = %u", new_node_id);
        return new_node_id;
    }
}


bool
ipset_bool_array_assignment(const void *user_data, ipset_variable variable)
{
    const bool  *bool_array = (const bool *) user_data;
    return bool_array[variable];
}


bool
ipset_bit_array_assignment(const void *user_data, ipset_variable variable)
{
    return IPSET_BIT_GET(user_data, variable);
}


ipset_value
ipset_node_evaluate(const struct ipset_node_cache *cache, ipset_node_id node_id,
                    ipset_assignment_func assignment, const void *user_data)
{
    ipset_node_id  curr_node_id = node_id;
    DEBUG("Evaluating BDD node %u", node_id);

    /* As long as the current node is a nonterminal, we have to check
     * the value of the current variable. */
    while (ipset_node_get_type(curr_node_id) == IPSET_NONTERMINAL_NODE) {
        /* We have to look up this variable in the assignment. */
        struct ipset_node  *node =
            ipset_node_cache_get_nonterminal(cache, curr_node_id);
        bool  this_value = assignment(user_data, node->variable);

        DEBUG("Variable %u has value %s", node->variable,
              this_value? "TRUE": "FALSE");

        if (this_value) {
            /* This node's variable is true in the assignment vector, so
             * trace down the high subtree. */
            curr_node_id = node->high;
        } else {
            /* This node's variable is false in the assignment vector,
             * so trace down the low subtree. */
            curr_node_id = node->low;
        }
    }

    /* Once we find a terminal node, we've got the final result. */
    DEBUG("Evaluated result is %u", ipset_terminal_value(curr_node_id));
    return ipset_terminal_value(curr_node_id);
}
