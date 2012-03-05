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


void
ipset_node_fprint(FILE *stream, struct ipset_node *node)
{
    fprintf(stream,
            "nonterminal(x%u? " IPSET_NODE_ID_FORMAT
            ": " IPSET_NODE_ID_FORMAT ")",
            node->variable,
            IPSET_NODE_ID_VALUES(node->high),
            IPSET_NODE_ID_VALUES(node->low));
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


/* The free list in an ipset_node_cache is represented by a
 * singly-linked list of indices into the chunk array.  Since the
 * ipset_node instance is unused for nodes in the free list, we reuse
 * the refcount field to store the "next" index. */

#define IPSET_NULL_INDEX ((ipset_variable) -1)

struct ipset_node_cache *
ipset_node_cache_new()
{
    struct ipset_node_cache  *cache = cork_new(struct ipset_node_cache);
    cork_array_init(&cache->chunks);
    cache->largest_index = 0;
    cache->free_list = IPSET_NULL_INDEX;
    cork_hash_table_init
        (&cache->node_cache, 0, ipset_node_hasher, ipset_node_comparator);
    return cache;
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
    free(cache);
}


/**
 * Returns the index of a new ipset_node instance.
 */
static ipset_value
ipset_node_cache_alloc_node(struct ipset_node_cache *cache)
{
    if (cache->free_list == IPSET_NULL_INDEX) {
        /* Nothing in the free list; need to allocate a new node. */
        ipset_value  next_index = cache->largest_index++;
        ipset_value  chunk_index = next_index >> IPSET_BDD_NODE_CACHE_BIT_SIZE;
        if (chunk_index >= cork_array_size(&cache->chunks)) {
            /* We've filled up all of the existing chunks, and need to
             * create a new one. */
            DEBUG("        (allocating chunk %zu)",
                  cork_array_size(&cache->chunks));
            struct ipset_node  *new_chunk = cork_calloc
                (IPSET_BDD_NODE_CACHE_SIZE, sizeof(struct ipset_node));
            cork_array_append(&cache->chunks, new_chunk);
        }
        return next_index;
    } else {
        /* Reuse a recently freed node. */
        ipset_value  next_index = cache->free_list;
        struct ipset_node  *node =
            ipset_node_cache_get_nonterminal_by_index(cache, next_index);
        cache->free_list = node->refcount;
        return next_index;
    }
}

ipset_node_id
ipset_node_incref(struct ipset_node_cache *cache, ipset_node_id node_id)
{
    if (ipset_node_get_type(node_id) == IPSET_NONTERMINAL_NODE) {
        struct ipset_node  *node =
            ipset_node_cache_get_nonterminal(cache, node_id);
        DEBUG("        [incref " IPSET_NODE_ID_FORMAT "]",
              IPSET_NODE_ID_VALUES(node_id));
        node->refcount++;
    }
    return node_id;
}

void
ipset_node_decref(struct ipset_node_cache *cache, ipset_node_id node_id)
{
    if (ipset_node_get_type(node_id) == IPSET_NONTERMINAL_NODE) {
        struct ipset_node  *node =
            ipset_node_cache_get_nonterminal(cache, node_id);
        DEBUG("        [decref " IPSET_NODE_ID_FORMAT "]",
              IPSET_NODE_ID_VALUES(node_id));
        if (--node->refcount == 0) {
            DEBUG("        [free   " IPSET_NODE_ID_FORMAT "]",
                  IPSET_NODE_ID_VALUES(node_id));
            ipset_node_decref(cache, node->low);
            ipset_node_decref(cache, node->high);
            cork_hash_table_delete(&cache->node_cache, node, NULL, NULL);

            /* Add the node to the free list */
            node->refcount = cache->free_list;
            cache->free_list = ipset_nonterminal_value(node_id);
        }
    }
}

ipset_node_id
ipset_node_cache_nonterminal(struct ipset_node_cache *cache,
                             ipset_variable variable,
                             ipset_node_id low, ipset_node_id high)
{
    /* Don't allow any nonterminals whose low and high subtrees are the
     * same, since the nonterminal would be redundant. */
    if (CORK_UNLIKELY(low == high)) {
        DEBUG("        [ SKIP  nonterminal(x%u? "
              IPSET_NODE_ID_FORMAT ": " IPSET_NODE_ID_FORMAT ")]",
              variable, IPSET_NODE_ID_VALUES(high), IPSET_NODE_ID_VALUES(low));
        ipset_node_decref(cache, high);
        return low;
    }

    /* Check to see if there's already a nonterminal with these contents
     * in the cache. */
    DEBUG("        [search nonterminal(x%u? "
          IPSET_NODE_ID_FORMAT ": " IPSET_NODE_ID_FORMAT ")]",
          variable, IPSET_NODE_ID_VALUES(high), IPSET_NODE_ID_VALUES(low));

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
        ipset_node_id  node_id = (uintptr_t) entry->value;
        DEBUG("        [reuse  " IPSET_NODE_ID_FORMAT "]",
              IPSET_NODE_ID_VALUES(node_id));
        ipset_node_incref(cache, node_id);
        ipset_node_decref(cache, low);
        ipset_node_decref(cache, high);
        return node_id;
    } else {
        /* This node doesn't exist yet.  Allocate a permanent copy of
         * the node, add it to the cache, and then return its ID. */
        ipset_value  new_index = ipset_node_cache_alloc_node(cache);
        ipset_node_id  new_node_id = ipset_nonterminal_node_id(new_index);
        struct ipset_node  *real_node =
            ipset_node_cache_get_nonterminal_by_index(cache, new_index);
        real_node->refcount = 1;
        real_node->variable = variable;
        real_node->low = low;
        real_node->high = high;
        entry->key = real_node;
        entry->value = (void *) (uintptr_t) new_node_id;
        DEBUG("        [new    " IPSET_NODE_ID_FORMAT "]",
              IPSET_NODE_ID_VALUES(new_node_id));
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
    DEBUG("Evaluating BDD node " IPSET_NODE_ID_FORMAT,
          IPSET_NODE_ID_VALUES(node_id));

    /* As long as the current node is a nonterminal, we have to check
     * the value of the current variable. */
    while (ipset_node_get_type(curr_node_id) == IPSET_NONTERMINAL_NODE) {
        /* We have to look up this variable in the assignment. */
        struct ipset_node  *node =
            ipset_node_cache_get_nonterminal(cache, curr_node_id);
        bool  this_value = assignment(user_data, node->variable);
        DEBUG("[%3u] Nonterminal " IPSET_NODE_ID_FORMAT,
              node->variable, IPSET_NODE_ID_VALUES(curr_node_id));
        DEBUG("[%3u]   x%u = %s",
              node->variable, node->variable, this_value? "TRUE": "FALSE");

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


static ipset_node_id
ipset_node_create_nonterminals(struct ipset_node_cache *cache,
                               ipset_node_id node,
                               ipset_assignment_func assignment,
                               const void *user_data, ipset_variable start,
                               ipset_variable finish, ipset_value value)
{
    ipset_variable  i;
    ipset_node_id  suffix_root;

    suffix_root = ipset_terminal_node_id(value);
    DEBUG("[%3u]   Creating new terminal = %u", start, value);
    DEBUG("[%3u]   Adding nonterminals for bits %u-%u", start, start, finish-1);

    for (i = finish; i-- > start; ) {
        ipset_node_id  low;
        ipset_node_id  high;

        if (assignment(user_data, i)) {
            /* Bit is set in key, so the high branch should
             * point towards our new terminal. */
            low = ipset_node_incref(cache, node);
            high = suffix_root;
        } else {
            /* Bit is NOT set in key, so the low branch should
             * point towards our new terminal. */
            low = suffix_root;
            high = ipset_node_incref(cache, node);
        }
        suffix_root = ipset_node_cache_nonterminal(cache, i, low, high);
        DEBUG("[%3u]   Creating nonterminal(x%u? "
                IPSET_NODE_ID_FORMAT ": " IPSET_NODE_ID_FORMAT
                ") => " IPSET_NODE_ID_FORMAT,
                i, i,
                IPSET_NODE_ID_VALUES(high), IPSET_NODE_ID_VALUES(low),
                IPSET_NODE_ID_VALUES(suffix_root));
    }

    return suffix_root;
}


static ipset_node_id
ipset_node_insert_(struct ipset_node_cache *cache, ipset_node_id node,
                   ipset_variable current_var, ipset_assignment_func assignment,
                   const void *user_data, ipset_variable var_count,
                   ipset_value value)
{
    if (ipset_node_get_type(node) == IPSET_NONTERMINAL_NODE) {
        struct ipset_node  *nonterminal =
            ipset_node_cache_get_nonterminal(cache, node);
        ipset_node_id  new_low;
        ipset_node_id  new_high;
        ipset_node_id  result;
        DEBUG("[%3u] Nonterminal " IPSET_NODE_ID_FORMAT,
              nonterminal->variable, IPSET_NODE_ID_VALUES(node));

        /* If this nonterminal's variable index is larger than the last
         * variable in the assignment, then the new value overrides the
         * entire BDD starting from the nonterminal. */
        if (nonterminal->variable >= var_count) {
            DEBUG("[%3u]   Last variable in assignment is %u",
                  nonterminal->variable, var_count-1);

            /* We might have to create new nonterminals, in case
             * there was a gap between the previous nonterminal and this
             * one. */
            return ipset_node_create_nonterminals
                (cache, node, assignment, user_data,
                 current_var, var_count, value);
        }

        if (assignment(user_data, nonterminal->variable)) {
            /* Bit is set in the key; recurse down the high branch */
            DEBUG("[%3u]   Recursing down high branch", nonterminal->variable);
            new_high = ipset_node_insert_
                (cache, nonterminal->high, nonterminal->variable + 1,
                 assignment, user_data, var_count, value);
            if (new_high == nonterminal->high) {
                ipset_node_decref(cache, new_high);
                return ipset_node_incref(cache, node);
            }
            new_low = ipset_node_incref(cache, nonterminal->low);
        } else {
            /* Bit is set in the key; recurse down the low branch */
            DEBUG("[%3u]   Recursing down low branch", nonterminal->variable);
            new_low = ipset_node_insert_
                (cache, nonterminal->low, nonterminal->variable + 1,
                 assignment, user_data, var_count, value);
            if (new_low == nonterminal->low) {
                ipset_node_decref(cache, new_low);
                return ipset_node_incref(cache, node);
            }
            new_high = ipset_node_incref(cache, nonterminal->high);
        }

        result = ipset_node_cache_nonterminal
            (cache, nonterminal->variable, new_low, new_high);

        DEBUG("[%3u]   Creating nonterminal(x%u? "
              IPSET_NODE_ID_FORMAT ": " IPSET_NODE_ID_FORMAT
              ") => " IPSET_NODE_ID_FORMAT,
              nonterminal->variable, nonterminal->variable,
              IPSET_NODE_ID_VALUES(new_high), IPSET_NODE_ID_VALUES(new_low),
              IPSET_NODE_ID_VALUES(result));
        return result;

    } else {
        ipset_value  terminal_value = ipset_terminal_value(node);
        DEBUG("[%3u] Terminal %u", current_var, terminal_value);

        if (terminal_value == value) {
            /* If the current node is a terminal, and its value matches
             * what we're trying to add, then this key is already in the
             * set. */
            DEBUG("[%3u]   Key is already in set", current_var);
            return ipset_node_incref(cache, node);
        } else {
            /* Otherwise we need to create nonterminal nodes for each of
             * the remaining bits in the key. */
            DEBUG("[%3u]   Key is NOT in set", current_var);
            return ipset_node_create_nonterminals
                (cache, node, assignment, user_data,
                 current_var, var_count, value);
        }
    }
}

ipset_node_id
ipset_node_insert(struct ipset_node_cache *cache, ipset_node_id node,
                  ipset_assignment_func assignment, const void *user_data,
                  ipset_variable var_count, ipset_value value)
{
    return ipset_node_insert_
        (cache, node, 0, assignment, user_data, var_count, value);
}
