/* -*- coding: utf-8 -*-
 * ----------------------------------------------------------------------
 * Copyright © 2009-2012, RedJack, LLC.
 * All rights reserved.
 *
 * Please see the LICENSE.txt file in this distribution for license
 * details.
 * ----------------------------------------------------------------------
 */

#include <libcork/core.h>

#include "ipset/bdd/nodes.h"
#include "ipset/ipset.h"
#include "internal.h"


struct ipset_node_cache *
ipset_cache = NULL;


int
ipset_init_library()
{
    if (CORK_UNLIKELY(ipset_cache == NULL)) {
        ipset_cache = ipset_node_cache_new();
        if (ipset_cache == NULL) {
            return -1;
        }
        return 0;
    } else {
        return 0;
    }
}
