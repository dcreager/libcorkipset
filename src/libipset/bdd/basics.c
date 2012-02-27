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
#include "ipset/logging.h"
#include "internal.h"


enum ipset_node_type
ipset_node_get_type(ipset_node_id node)
{
    /*
     * The ID of a terminal node has its LSB set to 1, and has the
     * terminal value stored in the remaining bits.  The ID of a
     * nonterminal node is simply a pointer to the node struct.
     */

    uintptr_t  node_int = (uintptr_t) node;

    if ((node_int & 1) == 1)
    {
        return IPSET_TERMINAL_NODE;
    } else {
        return IPSET_NONTERMINAL_NODE;
    }
}


ipset_range
ipset_terminal_value(ipset_node_id node_id)
{
    /*
     * The ID of a terminal node has its LSB set to 1, and has the
     * terminal value stored in the remaining bits.
     */

    unsigned int  node_int = (uintptr_t) node_id;
    return (int) (node_int >> 1);
}


struct ipset_node *
ipset_nonterminal_node(ipset_node_id node_id)
{
    /*
     * The ID of a nonterminal node is simply a pointer to the node
     * struct.
     */

    return (void *) (uintptr_t) node_id;
}


void
ipset_node_fprint(FILE *stream, struct ipset_node *node)
{
    fprintf(stream, "nonterminal(%u,%p,%p)",
            node->variable, node->low, node->high);
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


void
ipset_node_cache_free(struct ipset_node_cache *cache)
{
    cork_hash_table_done(&cache->node_cache);
    cork_hash_table_done(&cache->and_cache);
    cork_hash_table_done(&cache->or_cache);
    cork_hash_table_done(&cache->ite_cache);
    free(cache);
}


ipset_node_id
ipset_node_cache_terminal(struct ipset_node_cache *cache, ipset_range value)
{
    /* The ID of a terminal node has its LSB set to 1, and has the
     * terminal value stored in the remaining bits. */
    DEBUG("Creating terminal node for %d", value);

    unsigned int  node_int = (unsigned int) value;
    node_int <<= 1;
    node_int |= 1;

    DEBUG("Node ID is %p", ((void *) (uintptr_t) node_int));
    return (void *) (uintptr_t) node_int;
}


ipset_node_id
ipset_node_cache_nonterminal(struct ipset_node_cache *cache,
                             ipset_variable variable,
                             ipset_node_id low, ipset_node_id high)
{
    /* Don't allow any nonterminals whose low and high subtrees are the
     * same, since the nonterminal would be redundant. */
    if (CORK_UNLIKELY(low == high)) {
        DEBUG("Skipping nonterminal(%u,%p,%p)", variable, low, high);
        return low;
    }

    /* Check to see if there's already a nonterminal with these contents
     * in the cache. */
    DEBUG("Searching for nonterminal(%u,%p,%p)", variable, low, high);

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
        DEBUG("Existing node, ID = %p", entry->key);
        return entry->key;
    } else {
        /* This node doesn't exist yet.  Allocate a permanent copy of
         * the node, add it to the cache, and then return its ID. */
        struct ipset_node  *real_node = cork_new(struct ipset_node);
        memcpy(real_node, &search_node, sizeof(struct ipset_node));
        entry->key = real_node;
        DEBUG("NEW node, ID = %p", real_node);
        return real_node;
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


ipset_range
ipset_node_evaluate(ipset_node_id node_id,
                    ipset_assignment_func assignment, const void *user_data)
{
    ipset_node_id  curr_node_id = node_id;
    DEBUG("Evaluating BDD node %p", node_id);

    /* As long as the current node is a nonterminal, we have to check
     * the value of the current variable. */
    while (ipset_node_get_type(curr_node_id) == IPSET_NONTERMINAL_NODE) {
        /* We have to look up this variable in the assignment. */
        struct ipset_node  *node = ipset_nonterminal_node(curr_node_id);
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
    DEBUG("Evaluated result is %d", ipset_terminal_value(curr_node_id));
    return ipset_terminal_value(curr_node_id);
}
