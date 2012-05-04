/* -*- coding: utf-8 -*-
 * ----------------------------------------------------------------------
 * Copyright © 2012, RedJack, LLC.
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
        ip->_.u8[i] = random_byte;
    }
}


static void
build_set(struct ip_set *set, long num_elements)
{
    long  i;

    ipset_init(set);
    for (i = 0; i < num_elements; i++) {
        struct cork_ipv4  ip;
        random_ip(&ip);
        ipset_ipv4_add(set, &ip);
    }
}


static void
one_test(struct ip_set *set, long num_queries)
{
    clock_t  start, end;
    double  cpu_time_used;
    double  queries_per_second;
    struct cork_ipv4  ipv4;

    start = clock();
    for (ipv4._.u32 = 0; ipv4._.u32 < num_queries; ipv4._.u32++) {
        ipset_contains_ipv4(set, &ipv4);
    }
    end = clock();

    cpu_time_used = ((double) (end - start)) / CLOCKS_PER_SEC;
    queries_per_second = ((double) num_queries) / cpu_time_used;

    fprintf(stdout, "%9lu%18.6lf%18.3lf\n",
            num_queries, cpu_time_used, queries_per_second);
}


int
main(int argc, const char **argv)
{
    struct ip_set  set;
    long  num_tests;
    long  num_elements;
    long  num_queries;
    long  i;

    if (argc != 4) {
        fprintf(stderr, "Usage: contains [# tests] [# elements] [# queries]\n");
        return -1;
    }

    num_tests = atol(argv[1]);
    num_elements = atol(argv[2]);
    num_queries = atol(argv[3]);

    fprintf(stderr, "Creating set with %lu elements.\n", num_elements);

    ipset_init_library();
    srandom(time(NULL));
    build_set(&set, num_elements);

    fprintf(stdout, "%9s%18s%18s\n",
            "queries", "cpu_time", "queries_per_sec");
    for (i = 0; i < num_tests; i++) {
        one_test(&set, num_queries);
    }

    ipset_done(&set);
    return 0;
}
