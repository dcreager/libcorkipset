/* -*- coding: utf-8 -*-
 * ----------------------------------------------------------------------
 * Copyright © 2009-2012, RedJack, LLC.
 * All rights reserved.
 *
 * Please see the LICENSE.txt file in this distribution for license
 * details.
 * ----------------------------------------------------------------------
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <libcork/core.h>
#include <ipset/ipset.h>


static inline void
random_ip(struct cork_ipv4 *ip)
{
    int  i;

    for (i = 0; i < sizeof(struct cork_ipv4); i++) {
        uint8_t  random_byte = random() & 0xff;
        ip->u8[i] = random_byte;
    }
}


static void
one_test(long num_elements)
{
    struct ip_set  set;
    long  i;
    size_t  size;
    double  size_per_element;

    ipset_init(&set);
    for (i = 0; i < num_elements; i++) {
        struct cork_ipv4  ip;
        random_ip(&ip);
        ipset_ipv4_add(&set, &ip);
    }

    size = ipset_memory_size(&set);
    size_per_element = ((double) size) / num_elements;
    fprintf(stdout, "%lu %zu %.3f\n", num_elements, size, size_per_element);

    ipset_done(&set);
}


int
main(int argc, const char **argv)
{
    long  num_tests;
    long  num_elements;
    long  i;

    if (argc != 3) {
        fprintf(stderr, "Usage: ipv4-set-size [# tests] [# elements]\n");
        return -1;
    }

    num_tests = atol(argv[1]);
    num_elements = atol(argv[2]);

    fprintf(stderr, "Creating %lu sets with %lu elements each.\n",
            num_tests, num_elements);

    ipset_init_library();
    srandom(time(NULL));

    for (i = 0; i < num_tests; i++) {
        one_test(num_elements);
    }

    return 0;
}
