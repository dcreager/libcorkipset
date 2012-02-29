/* -*- coding: utf-8 -*-
 * ----------------------------------------------------------------------
 * Copyright © 2010-2012, RedJack, LLC.
 * All rights reserved.
 *
 * Please see the LICENSE.txt file in this distribution for license
 * details.
 * ----------------------------------------------------------------------
 */

#include <libcork/core.h>
#include <libcork/ds.h>

#include "ipset/bdd/nodes.h"
#include "ipset/logging.h"


static cork_hash
constant_hasher(const void *key)
{
    return (uintptr_t) key;
}

static bool
constant_comparator(const void *key1, const void *key2)
{
    return key1 == key2;
}

size_t
ipset_node_reachable_count(ipset_node_id node)
{
    /* Create a set to track when we've visited a given node. */
    struct cork_hash_table  visited;
    cork_hash_table_init(&visited, 0, constant_hasher, constant_comparator);

    /* And a queue of nodes to check. */
    cork_array(ipset_node_id)  queue;
    cork_array_init(&queue);

    if (ipset_node_get_type(node) == IPSET_NONTERMINAL_NODE) {
        DEBUG("Adding node %p to queue", node);
        cork_array_append(&queue, node);
    }

    /* And somewhere to store the result. */
    size_t  node_count = 0;

    /* Check each node in turn. */
    while (!cork_array_is_empty(&queue)) {
        ipset_node_id  curr = cork_array_at(&queue, --queue.size);

        /* We don't have to do anything if this node is already in the
         * visited set. */
        if (cork_hash_table_get(&visited, curr) == NULL) {
            DEBUG("Visiting node %p for the first time", curr);

            /* Add the node to the visited set. */
            cork_hash_table_put
                (&visited, curr, (void *) (intptr_t) true, NULL, NULL, NULL);

            /* Increase the node count. */
            node_count++;

            /* And add the node's nonterminal children to the visit
             * queue. */
            struct ipset_node  *node = ipset_nonterminal_node(curr);

            if (ipset_node_get_type(node->low) == IPSET_NONTERMINAL_NODE) {
                DEBUG("Adding node %p to queue", node->low);
                cork_array_append(&queue, node->low);
            }

            if (ipset_node_get_type(node->high) == IPSET_NONTERMINAL_NODE) {
                DEBUG("Adding node %p to queue", node->high);
                cork_array_append(&queue, node->high);
            }
        }
    }

    /* Return the result, freeing everything before we go. */
    cork_hash_table_done(&visited);
    cork_array_done(&queue);
    return node_count;
}


size_t
ipset_node_memory_size(ipset_node_id node)
{
    return ipset_node_reachable_count(node) * sizeof(struct ipset_node);
}
