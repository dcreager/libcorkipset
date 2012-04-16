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

bool
ipset_node_cache_nodes_equal(const struct ipset_node_cache *cache1,
                             ipset_node_id node_id1,
                             const struct ipset_node_cache *cache2,
                             ipset_node_id node_id2)
{
    struct ipset_node  *node1;
    struct ipset_node  *node2;

    if (ipset_node_get_type(node_id1) != ipset_node_get_type(node_id2)) {
        return false;
    }

    if (ipset_node_get_type(node_id1) == IPSET_TERMINAL_NODE) {
        return node_id1 == node_id2;
    }

    node1 = ipset_node_cache_get_nonterminal(cache1, node_id1);
    node2 = ipset_node_cache_get_nonterminal(cache2, node_id2);
    return
        (node1->variable == node2->variable) &&
        ipset_node_cache_nodes_equal(cache1, node1->low, cache2, node2->low) &&
        ipset_node_cache_nodes_equal(cache1, node1->high, cache2, node2->high);
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


/* A “fake” BDD node given by an assignment. */
struct ipset_fake_node {
    ipset_variable  current_var;
    ipset_variable  var_count;
    ipset_assignment_func  assignment;
    const void  *user_data;
    ipset_value  value;
};

/* We add elements to a set using the logical or (||) operator:
 *
 *   new_set = new_element || old_set
 *
 * (This is the short-circuit ||, so new_element's value takes
 * precedence.)
 *
 * The below is a straight copy of the standard binary APPLY from the
 * BDD literature, but without the caching of the results.  And also
 * with the wrinkle that the LHS argument to ITE (i.e., new_element) is
 * given by an assignment, and not by a BDD node.  (This lets us skip
 * constructing the BDD for the assignment, saving us a few cycles.)
 */

static ipset_node_id
ipset_apply_or(struct ipset_node_cache *cache, struct ipset_fake_node *lhs,
               ipset_node_id rhs);

static ipset_node_id
recurse_left(struct ipset_node_cache *cache, struct ipset_fake_node *lhs,
             ipset_node_id rhs)
{
    ipset_node_id  result_low;
    ipset_node_id  result_high;

    if (lhs->assignment(lhs->user_data, lhs->current_var)) {
        /* Since this bit is set in the assignment, the LHS's high
         * branch is a true recursion, and its low branch points at the
         * 0 terminal. */
        DEBUG("[%3u]   x[%u] is set", lhs->current_var, lhs->current_var);
        DEBUG("[%3u]   Recursing high", lhs->current_var);
        lhs->current_var++;
        result_high = ipset_apply_or(cache, lhs, rhs);
        lhs->current_var--;
        DEBUG("[%3u]   Back from high recursion", lhs->current_var);
        result_low = ipset_node_incref(cache, rhs);
    } else {
        /* and vice versa when the bit is unset */
        DEBUG("[%3u]   x[%u] is not set", lhs->current_var, lhs->current_var);
        DEBUG("[%3u]   Recursing low", lhs->current_var);
        lhs->current_var++;
        result_low = ipset_apply_or(cache, lhs, rhs);
        lhs->current_var--;
        DEBUG("[%3u]   Back from low recursion", lhs->current_var);
        result_high = ipset_node_incref(cache, rhs);
    }

    return ipset_node_cache_nonterminal
        (cache, lhs->current_var, result_low, result_high);
}

static ipset_node_id
recurse_right(struct ipset_node_cache *cache, struct ipset_fake_node *lhs,
              struct ipset_node *rhs)
{
    ipset_node_id  result_low;
    ipset_node_id  result_high;

    DEBUG("[%3u]   Recursing low", lhs->current_var);
    result_low = ipset_apply_or(cache, lhs, rhs->low);
    DEBUG("[%3u]   Back from low recursion", lhs->current_var);
    DEBUG("[%3u]   Recursing high", lhs->current_var);
    result_high = ipset_apply_or(cache, lhs, rhs->high);
    DEBUG("[%3u]   Back from high recursion", lhs->current_var);

    return ipset_node_cache_nonterminal
        (cache, lhs->current_var, result_low, result_high);
}

static ipset_node_id
recurse_both(struct ipset_node_cache *cache, struct ipset_fake_node *lhs,
             struct ipset_node *rhs)
{
    ipset_node_id  result_low;
    ipset_node_id  result_high;
    struct ipset_fake_node  other = {
        lhs->var_count, lhs->var_count, lhs->assignment, lhs->user_data, 0
    };

    if (lhs->assignment(lhs->user_data, lhs->current_var)) {
        /* Since this bit is set in the assignment, the LHS's high
         * branch is a true recursion, and its low branch points at the
         * 0 terminal. */
        DEBUG("[%3u]   x[%u] is set", lhs->current_var, lhs->current_var);
        DEBUG("[%3u]   Recursing high", lhs->current_var);
        lhs->current_var++;
        result_high = ipset_apply_or(cache, lhs, rhs->high);
        lhs->current_var--;
        DEBUG("[%3u]   Back from high recursion", lhs->current_var);
        DEBUG("[%3u]   Recursing low", lhs->current_var);
        result_low = ipset_apply_or(cache, &other, rhs->low);
        DEBUG("[%3u]   Back from low recursion", lhs->current_var);
    } else {
        /* and vice versa when the bit is unset */
        DEBUG("[%3u]   x[%u] is not set", lhs->current_var, lhs->current_var);
        DEBUG("[%3u]   Recursing low", lhs->current_var);
        lhs->current_var++;
        result_low = ipset_apply_or(cache, lhs, rhs->low);
        lhs->current_var--;
        DEBUG("[%3u]   Back from low recursion", lhs->current_var);
        DEBUG("[%3u]   Recursing high", lhs->current_var);
        result_high = ipset_apply_or(cache, &other, rhs->high);
        DEBUG("[%3u]   Back from high recursion", lhs->current_var);
    }

    return ipset_node_cache_nonterminal
        (cache, lhs->current_var, result_low, result_high);
}

static ipset_node_id
ipset_apply_or(struct ipset_node_cache *cache, struct ipset_fake_node *lhs,
               ipset_node_id rhs)
{
    ipset_variable  current_var = lhs->current_var;

    /* If LHS is a terminal, then we're in one of the following two
     * cases:
     *
     *   0 || Y = Y
     *   X || Y = X
     */
    if (lhs->current_var == lhs->var_count) {
        ipset_node_id  result;
        DEBUG("[%3u] LHS is terminal (value %u)", current_var, lhs->value);

        if (lhs->value == 0) {
            result = ipset_node_incref(cache, rhs);
            DEBUG("[%3u] 0 || " IPSET_NODE_ID_FORMAT
                  " = " IPSET_NODE_ID_FORMAT,
                  current_var, IPSET_NODE_ID_VALUES(result),
                  IPSET_NODE_ID_VALUES(result));
        } else {
            result = ipset_terminal_node_id(lhs->value);
            DEBUG("[%3u] %u || " IPSET_NODE_ID_FORMAT " = %u",
                  current_var, lhs->value,
                  IPSET_NODE_ID_VALUES(result), lhs->value);
        }

        return result;
    }

    /* From here to the end of the function, we know that LHS is a
     * nonterminal. */
    DEBUG("[%3u] LHS is nonterminal", current_var);

    if (ipset_node_get_type(rhs) == IPSET_TERMINAL_NODE) {
        /* When one node (RHS) is terminal, and the other is nonterminal
         * (LHS), then we recurse down the subtrees of the nonterminal,
         * combining the results with the terminal. */
        DEBUG("[%3u] RHS is terminal(%u), recursing left",
              current_var, ipset_terminal_value(rhs));
        return recurse_left(cache, lhs, rhs);

    } else {
        /* When both nodes are nonterminal, the way we recurse depends
         * on the variables of the nonterminals.  We always recurse down
         * the nonterminal(s) with the smaller variable index.  This
         * ensures that our BDDs remain ordered. */
        struct ipset_node  *rhs_node =
            ipset_node_cache_get_nonterminal(cache, rhs);

        if (current_var == rhs_node->variable) {
            DEBUG("[%3u] RHS is nonterminal(%u), recursing both",
                  current_var, rhs_node->variable);
            return recurse_both(cache, lhs, rhs_node);
        } else if (current_var < rhs_node->variable) {
            DEBUG("[%3u] RHS is nonterminal(%u), recursing left",
                  current_var, rhs_node->variable);
            return recurse_left(cache, lhs, rhs);
        } else {
            DEBUG("[%3u] RHS is nonterminal(%u), recursing right",
                  current_var, rhs_node->variable);
            return recurse_right(cache, lhs, rhs_node);
        }
    }
}

ipset_node_id
ipset_node_insert(struct ipset_node_cache *cache, ipset_node_id node,
                  ipset_assignment_func assignment, const void *user_data,
                  ipset_variable var_count, ipset_value value)
{
    struct ipset_fake_node  lhs = { 0, var_count, assignment, user_data, value };
    DEBUG("Inserting new element");
    return ipset_apply_or(cache, &lhs, node);
}
