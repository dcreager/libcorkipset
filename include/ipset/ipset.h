/* -*- coding: utf-8 -*-
 * ----------------------------------------------------------------------
 * Copyright © 2009-2012, RedJack, LLC.
 * All rights reserved.
 *
 * Please see the LICENSE.txt file in this distribution for license
 * details.
 * ----------------------------------------------------------------------
 */

#ifndef IPSET_IPSET_H
#define IPSET_IPSET_H

#include <stdio.h>

#include <libcork/core.h>
#include <libcork/ds.h>

#include <ipset/bdd/nodes.h>


struct ip_set {
    ipset_node_id  set_bdd;
};


struct ip_map {
    ipset_node_id  map_bdd;
    ipset_node_id  default_bdd;
};


/*---------------------------------------------------------------------
 * General functions
 */

/**
 * Initializes the library.  Must be called before any other ipset
 * function.  Can safely be called multiple times.
 */
int
ipset_init_library();


/*---------------------------------------------------------------------
 * IP set functions
 */

/**
 * Initializes a new IP set that has already been allocated (on the
 * stack, for instance).  After returning, the set will be empty.
 */
void
ipset_init(struct ip_set *set);

/**
 * Finalize an IP set, freeing any space used to represent the set
 * internally.  Doesn't deallocate the struct ip_set itself, so this is
 * safe to call on stack-allocated sets.
 */
void
ipset_done(struct ip_set *set);

/**
 * Creates a new empty IP set on the heap.  Returns NULL if we can't
 * allocate a new instance.
 */
struct ip_set *
ipset_new(void);

/**
 * Finalize and free a heap-allocated IP set, freeing any space used
 * to represent the set internally.
 */
void
ipset_free(struct ip_set *set);

/**
 * Returns whether the IP set is empty.
 */
bool
ipset_is_empty(const struct ip_set *set);

/**
 * Returns whether two IP sets are equal.
 */
bool
ipset_is_equal(const struct ip_set *set1, const struct ip_set *set2);

/**
 * Returns the number of bytes needed to store the IP set.  Note that
 * adding together the storage needed for each set you use doesn't
 * necessarily give you the total memory requirements, since some
 * storage can be shared between sets.
 */
size_t
ipset_memory_size(const struct ip_set *set);

/**
 * Saves an IP set to disk.  Returns a boolean indicating whether the
 * operation was successful.
 */
int
ipset_save(FILE *stream, const struct ip_set *set);

/**
 * Saves an IP set to a stream consumer.  Returns a boolean indicating
 * whether the operation was successful.
 */
int
ipset_save_to_stream(struct cork_stream_consumer *stream,
                     const struct ip_set *set);

/**
 * Saves a GraphViz dot graph for an IP set to disk.  Returns a
 * boolean indicating whether the operation was successful.
 */
int
ipset_save_dot(FILE *stream, const struct ip_set *set);

/**
 * Loads an IP set from a stream.  Returns NULL if the set cannot be
 * loaded.
 */
struct ip_set *
ipset_load(FILE *stream);

/**
 * Adds a single IPv4 address to an IP set.  Returns whether the value
 * was already in the set or not.
 */
bool
ipset_ipv4_add(struct ip_set *set, struct cork_ipv4 *elem);

/**
 * Adds a network of IPv4 addresses to an IP set.  All of the addresses
 * that start with the first cidr_prefix bits of elem will be added to
 * the set.  Returns whether the network was already in the set or not.
 */

bool
ipset_ipv4_add_network(struct ip_set *set, struct cork_ipv4 *elem,
                       unsigned int cidr_prefix);

/**
 * Returns whether the given IPv4 address is in an IP set.
 */
bool
ipset_contains_ipv4(const struct ip_set *set, struct cork_ipv4 *elem);

/**
 * Adds a single IPv6 address to an IP set.  Returns whether the value
 * was already in the set or not.
 */

bool
ipset_ipv6_add(struct ip_set *set, struct cork_ipv6 *elem);

/**
 * Adds a network of IPv6 addresses to an IP set.  All of the addresses
 * that start with the first cidr_prefix bits of elem will be added to
 * the set.  Returns whether the network was already in the set or not.
 */

bool
ipset_ipv6_add_network(struct ip_set *set, struct cork_ipv6 *elem,
                       unsigned int cidr_prefix);

/**
 * Returns whether the given IPv6 address is in an IP set.
 */
bool
ipset_contains_ipv6(const struct ip_set *set, struct cork_ipv6 *elem);

/**
 * Adds a single generic IP address to an IP set.  Returns whether the
 * value was already in the set or not.
 */

bool
ipset_ip_add(struct ip_set *set, struct cork_ip *addr);

/**
 * Adds a network of generic IP addresses to an IP set.  All of the
 * addresses that start with the first cidr_prefix bits of elem will be
 * added to the set.  Returns whether the network was already in the set
 * or not.
 */

bool
ipset_ip_add_network(struct ip_set *set, struct cork_ip *addr,
                     unsigned int cidr_prefix);

/**
 * Returns whether the given generic IP address is in an IP set.
 */
bool
ipset_contains_ip(const struct ip_set *set, struct cork_ip *elem);


/**
 * An internal state type used by the
 * ipset_iterator_multiple_expansion_state field.
 */
enum ipset_iterator_state {
    IPSET_ITERATOR_NORMAL = 0,
    IPSET_ITERATOR_MULTIPLE_IPV4,
    IPSET_ITERATOR_MULTIPLE_IPV6
};


/**
 * An iterator that returns all of the IP addresses that are (or are
 * not) in an IP set.
 */
struct ipset_iterator {
    /* Whether there are any more IP addresses in this iterator. */
    bool  finished;

    /* The desired value for each IP address. */
    bool  desired_value;

    /* Whether to summarize the contents of the IP set as networks,
     * where possible. */
    bool  summarize;

    /* Whether the current assignment needs to be expanded a second
     * time.
     *
     * We have to expand IPv4 and IPv6 assignments separately, since the
     * set of variables to turn into address bits is different.
     * Unfortunately, a BDD assignment can contain both IPv4 and IPv6
     * addresses, if variable 0 is EITHER.  (This is trivially true for
     * the empty set, for instance.)  In this case, we have to
     * explicitly set variable 0 to TRUE, expand it as IPv4, and then
     * set it to FALSE, and expand it as IPv6.  This variable tells us
     * whether we're in an assignment that needs to be expanded twice,
     * and if so, which expansion we're currently in.
     */
    enum ipset_iterator_state  multiple_expansion_state;

    /* An iterator for retrieving each assignment in the set's BDD. */
    struct ipset_bdd_iterator  *bdd_iterator;

    /* An iterator for expanding each assignment into individual IP
     * addresses. */
    struct ipset_expanded_assignment  *assignment_iterator;

    /* The address of the current IP network in the iterator. */
    struct cork_ip  addr;

    /* The netmask of the current IP network in the iterator, given as a
     * CIDR prefix.  For a single IP address, this will be 32 or 128. */
    unsigned int  cidr_prefix;
};


/**
 * Return an iterator that yields all of the IP addresses that are (if
 * desired_value is true) or are not (if desired_value is false) in an
 * IP set.
 */
struct ipset_iterator *
ipset_iterate(struct ip_set *set, bool desired_value);


/**
 * Return an iterator that yields all of the IP networks that are (if
 * desired_value is true) or are not (if desired_value is false) in an
 * IP set.
 */
struct ipset_iterator *
ipset_iterate_networks(struct ip_set *set, bool desired_value);


/**
 * Free an IP set iterator.
 */
void
ipset_iterator_free(struct ipset_iterator *iterator);


/**
 * Advance an IP set iterator to the next IP address.
 */
void
ipset_iterator_advance(struct ipset_iterator *iterator);


/*---------------------------------------------------------------------
 * IP map functions
 */

/**
 * Initializes a new IP map that has already been allocated (on the
 * stack, for instance).  After returning, the map will be empty.  Any
 * addresses that aren't explicitly added to the map will have
 * default_value as their value.
 */
void
ipmap_init(struct ip_map *map, int default_value);

/**
 * Finalize an IP map, freeing any space used to represent the map
 * internally.  Doesn't deallocate the struct ip_map itself, so this is
 * safe to call on stack-allocated maps.
 */
void
ipmap_done(struct ip_map *map);

/**
 * Creates a new empty IP map on the heap.  Returns NULL if we can't
 * allocate a new instance.  Any addresses that aren't explicitly
 * added to the map will have default_value as their value.
 */
struct ip_map *
ipmap_new(int default_value);

/**
 * Finalize and free a heap-allocated IP map, freeing any space used
 * to represent the map internally.
 */
void
ipmap_free(struct ip_map *map);

/**
 * Returns whether the IP map is empty.  A map is considered empty if
 * every input is mapped to the default value.
 */
bool
ipmap_is_empty(const struct ip_map *map);

/**
 * Returns whether two IP maps are equal.
 */
bool
ipmap_is_equal(const struct ip_map *map1, const struct ip_map *map2);

/**
 * Returns the number of bytes needed to store the IP map.  Note that
 * adding together the storage needed for each map you use doesn't
 * necessarily give you the total memory requirements, since some
 * storage can be shared between maps.
 */
size_t
ipmap_memory_size(const struct ip_map *map);

/**
 * Saves an IP map to disk.  Returns a boolean indicating whether the
 * operation was successful.
 */
int
ipmap_save(FILE *stream, const struct ip_map *map);

/**
 * Saves an IP map to a stream consumer.  Returns a boolean indicating
 * whether the operation was successful.
 */
int
ipmap_save_to_stream(struct cork_stream_consumer *stream,
                     const struct ip_map *map);

/**
 * Loads an IP map from disk.  Returns NULL if the map cannot be
 * loaded.
 */
struct ip_map *
ipmap_load(FILE *stream);

/**
 * Adds a single IPv4 address to an IP map, with the given value.
 */
void
ipmap_ipv4_set(struct ip_map *map, struct cork_ipv4 *elem, int value);

/**
 * Adds a network of IPv4 addresses to an IP map, with each address in
 * the network mapping to the given value.  All of the addresses that
 * start with the first cidr_prefix bits of elem will be added to the
 * map.
 */
void
ipmap_ipv4_set_network(struct ip_map *map, struct cork_ipv4 *elem,
                       unsigned int cidr_prefix, int value);

/**
 * Returns the value that an IPv4 address is mapped to in the map.
 */
int
ipmap_ipv4_get(struct ip_map *map, struct cork_ipv4 *elem);

/**
 * Adds a single IPv6 address to an IP map, with the given value.
 */
void
ipmap_ipv6_set(struct ip_map *map, struct cork_ipv6 *elem, int value);

/**
 * Adds a network of IPv6 addresses to an IP map, with each address in
 * the network mapping to the given value.  All of the addresses that
 * start with the first cidr_prefix bits of elem will be added to the
 * map.
 */
void
ipmap_ipv6_set_network(struct ip_map *map, struct cork_ipv6 *elem,
                       unsigned int cidr_prefix, int value);

/**
 * Returns the value that an IPv6 address is mapped to in the map.
 */
int
ipmap_ipv6_get(struct ip_map *map, struct cork_ipv6 *elem);

/**
 * Adds a single generic IP address to an IP map, with the given value.
 */
void
ipmap_ip_set(struct ip_map *map, struct cork_ip *addr, int value);

/**
 * Adds a network of generic IP addresses to an IP map, with each
 * address in the network mapping to the given value.  All of the
 * addresses that start with the first cidr_prefix bits of elem will be
 * added to the map.
 */
void
ipmap_ip_set_network(struct ip_map *map, struct cork_ip *addr,
                     unsigned int cidr_prefix, int value);

/**
 * Returns the value that a generic IP address is mapped to in the
 * map.
 */
int
ipmap_ip_get(struct ip_map *map, struct cork_ip *addr);


#endif  /* IPSET_IPSET_H */
