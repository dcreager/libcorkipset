/* -*- coding: utf-8 -*-
 * ----------------------------------------------------------------------
 * Copyright © 2012, RedJack, LLC.
 * All rights reserved.
 *
 * Please see the LICENSE.txt file in this distribution for license
 * details.
 * ----------------------------------------------------------------------
 */

#include "ipset/bdd/nodes.h"


/**
 * The key for a cache that memoizes the results of a binary BDD
 * operator.
 */
struct ipset_binary_key {
    ipset_node_id  lhs;
    ipset_node_id  rhs;
};


/**
 * The key for a cache that memoizes the results of a trinary BDD
 * operator.
 */
struct ipset_trinary_key {
    ipset_node_id  f;
    ipset_node_id  g;
    ipset_node_id  h;
} ipset_trinary_key;
