/* -*- coding: utf-8 -*-
 * ----------------------------------------------------------------------
 * Copyright © 2010-2012, RedJack, LLC.
 * All rights reserved.
 *
 * Please see the LICENSE.txt file in this distribution for license
 * details.
 * ----------------------------------------------------------------------
 */

#include <string.h>

#include <libcork/core.h>
#include <libcork/ds.h>

#include "ipset/bdd/nodes.h"
#include "ipset/logging.h"
#include "internal.h"


static void
ipset_binary_key_commutative(struct ipset_binary_key *key,
                             ipset_node_id lhs, ipset_node_id rhs)
{
    /* Since the operator is commutative, make sure that the LHS is
     * smaller than the RHS. */
    if (lhs < rhs) {
        key->lhs = lhs;
        key->rhs = rhs;
    } else {
        key->lhs = rhs;
        key->rhs = lhs;
    }
}


/**
 * A function that defines how the BDD operation is applied to two
 * terminal nodes.
 */
typedef ipset_range
(*operator_func)(ipset_range lhs_value, ipset_range rhs_value);


/* forward declaration */
static ipset_node_id
cached_op(struct ipset_node_cache *cache, struct cork_hash_table *op_cache,
          operator_func op, const char *op_name,
          ipset_node_id lhs, ipset_node_id rhs);


/**
 * Recurse down one subtree (the LHS).
 */
static ipset_node_id
recurse_left(struct ipset_node_cache *cache, struct cork_hash_table *op_cache,
             operator_func op, const char *op_name,
             struct ipset_node *lhs_node, ipset_node_id rhs)
{
    ipset_node_id  result_low =
        cached_op(cache, op_cache, op, op_name, lhs_node->low, rhs);
    ipset_node_id  result_high =
        cached_op(cache, op_cache, op, op_name, lhs_node->high, rhs);

    return ipset_node_cache_nonterminal
        (cache, lhs_node->variable, result_low, result_high);
}


/**
 * Recurse down both subtrees simultaneously.
 */
static ipset_node_id
recurse_both(struct ipset_node_cache *cache, struct cork_hash_table *op_cache,
             operator_func op, const char *op_name,
             struct ipset_node *lhs_node, struct ipset_node *rhs_node)
{
    ipset_node_id  result_low =
        cached_op(cache, op_cache, op, op_name, lhs_node->low, rhs_node->low);
    ipset_node_id  result_high =
        cached_op(cache, op_cache, op, op_name, lhs_node->high, rhs_node->high);

    return ipset_node_cache_nonterminal
        (cache, lhs_node->variable, result_low, result_high);
}


/**
 * Perform an actual binary operation.
 */
static ipset_node_id
apply_op(struct ipset_node_cache *cache, struct cork_hash_table *op_cache,
         operator_func op, const char *op_name,
         ipset_node_id lhs, ipset_node_id rhs)
{
    if (ipset_node_get_type(lhs) == IPSET_TERMINAL_NODE) {
        if (ipset_node_get_type(rhs) == IPSET_TERMINAL_NODE) {
            /* When both nodes are terminal, we apply the operator to
             * the terminals' values, and construct a new terminal from
             * the result.  Note that we do not verify that the operator
             * returns a positive value. */
            ipset_range  lhs_value = ipset_terminal_value(lhs);
            ipset_range  rhs_value = ipset_terminal_value(rhs);
            ipset_range  new_value = op(lhs_value, rhs_value);
            return ipset_node_cache_terminal(cache, new_value);
        } else {
            /* When one node is terminal, and the other is nonterminal,
             * we recurse down the subtrees of the nonterminal,
             * combining the results with the terminal. */
            struct ipset_node  *rhs_node = ipset_nonterminal_node(rhs);
            return recurse_left
                (cache, op_cache, op, op_name, rhs_node, lhs);
        }
    } else {
        if (ipset_node_get_type(rhs) == IPSET_TERMINAL_NODE) {
            /* When one node is terminal, and the other is nonterminal,
             * we recurse down the subtrees of the nonterminal,
             * combining the results with the terminal. */
            struct ipset_node  *lhs_node = ipset_nonterminal_node(lhs);
            return recurse_left
                (cache, op_cache, op, op_name, lhs_node, rhs);
        } else {
            /* When both nodes are nonterminal, the way we recurse
             * depends on the variables of the nonterminals.  We always
             * recurse down the nonterminal with the smaller variable
             * index.  This ensures that our BDDs remain ordered. */
            struct ipset_node  *lhs_node = ipset_nonterminal_node(lhs);
            struct ipset_node  *rhs_node = ipset_nonterminal_node(rhs);

            if (lhs_node->variable == rhs_node->variable) {
                return recurse_both
                    (cache, op_cache, op, op_name, lhs_node, rhs_node);
            } else if (lhs_node->variable < rhs_node->variable) {
                return recurse_left
                    (cache, op_cache, op, op_name, lhs_node, rhs);
            } else {
                return recurse_left
                    (cache, op_cache, op, op_name, rhs_node, lhs);
            }
        }
    }
}


/**
 * Perform an actual binary operation, checking the cache first.
 */

static ipset_node_id
cached_op(struct ipset_node_cache *cache, struct cork_hash_table *op_cache,
          operator_func op, const char *op_name,
          ipset_node_id lhs, ipset_node_id rhs)
{
    /* Check to see if we've already performed the operation on these
     * operands. */
    DEBUG("Applying %s(%p, %p)", op_name, lhs, rhs);

    struct cork_hash_table_entry  *entry;
    bool  is_new;
    struct ipset_binary_key  search_key;
    ipset_binary_key_commutative(&search_key, lhs, rhs);

    entry = cork_hash_table_get_or_create(op_cache, &search_key, &is_new);

    if (!is_new) {
        /* There's a result in the cache, so return it. */
        DEBUG("Existing result = %p", entry->value);
        return entry->value;
    } else {
        /* This result doesn't exist yet.  Allocate a permanent copy of
         * the key.  Apply the operator, add the result to the cache,
         * and then return it. */

        struct ipset_binary_key  *real_key = cork_new(struct ipset_binary_key);
        memcpy(real_key, &search_key, sizeof(struct ipset_binary_key));
        entry->key = real_key;
        entry->value = apply_op(cache, op_cache, op, op_name, lhs, rhs);
        DEBUG("NEW result = %p", entry->value);
        return entry->value;
    }
}


static ipset_range
and_op(ipset_range lhs_value, ipset_range rhs_value)
{
    return (lhs_value & rhs_value);
}


ipset_node_id
ipset_node_cache_and(struct ipset_node_cache *cache,
                     ipset_node_id lhs, ipset_node_id rhs)
{
    return cached_op(cache, &cache->and_cache, and_op, "AND", lhs, rhs);
}


static ipset_range
or_op(ipset_range lhs_value, ipset_range rhs_value)
{
    return (lhs_value | rhs_value);
}


ipset_node_id
ipset_node_cache_or(struct ipset_node_cache *cache,
                    ipset_node_id lhs, ipset_node_id rhs)
{
    return cached_op(cache, &cache->or_cache, or_op, "OR", lhs, rhs);
}
