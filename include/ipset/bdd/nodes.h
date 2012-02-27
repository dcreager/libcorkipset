/* -*- coding: utf-8 -*-
 * ----------------------------------------------------------------------
 * Copyright © 2010-2012, RedJack, LLC.
 * All rights reserved.
 *
 * Please see the LICENSE.txt file in this distribution for license
 * details.
 * ----------------------------------------------------------------------
 */

#ifndef IPSET_BDD_NODES_H
#define IPSET_BDD_NODES_H


#include <stdio.h>

#include <libcork/core.h>
#include <libcork/ds.h>


/*-----------------------------------------------------------------------
 * Error reporting
 */

/* Hash of "ipset.h" */
#define IPSET_ERROR  0xf2000181

enum ipset_error {
    IPSET_IO_ERROR,
    IPSET_PARSE_ERROR
};


/*-----------------------------------------------------------------------
 * Bit arrays
 */

/**
 * Extract the byte that contains a particular bit in an array.
 */
#define IPSET_BIT_GET_BYTE(array, i)            \
    (((uint8_t *) (array))[(i) / 8])

/**
 * Create a bit mask that extracts a particular bit from the byte that
 * it lives in.
 */
#define IPSET_BIT_ON_MASK(i)                    \
    (0x80 >> ((i) % 8))

/**
 * Create a bit mask that extracts everything except for a particular
 * bit from the byte that it lives in.
 */
#define IPSET_BIT_NEG_MASK(i)                   \
    (~IPSET_BIT_ON_MASK(i))

/**
 * Return whether a particular bit is set in a byte array.  Bits are
 * numbered from 0, in a big-endian order.
 */
#define IPSET_BIT_GET(array, i)                 \
    ((IPSET_BIT_GET_BYTE(array, i) &            \
      IPSET_BIT_ON_MASK(i)) != 0)

/**
 * Set (or unset) a particular bit is set in a byte array.  Bits are
 * numbered from 0, in a big-endian order.
 */
#define IPSET_BIT_SET(array, i, val)                           \
    (IPSET_BIT_GET_BYTE(array, i) =                            \
     (IPSET_BIT_GET_BYTE(array, i) & IPSET_BIT_NEG_MASK(i))    \
     | ((val)? IPSET_BIT_ON_MASK(i): 0))


/*-----------------------------------------------------------------------
 * Preliminaries
 */

/**
 * Each variable in a BDD is referred to by number.
 */
typedef unsigned int  ipset_variable;


/**
 * Each BDD terminal represents an integer value.  The integer must be
 * non-negative, but must be within the range of the <i>signed</i>
 * integer type.
 */
typedef int  ipset_range;


/**
 * An identifier for each distinct node in a BDD.
 *
 * Internal implementation note.  Since pointers are aligned to at
 * least two bytes, the ID of a terminal node has its LSB set to 1,
 * and has the terminal value stored in the remaining bits.  The ID of
 * a nonterminal node is simply a pointer to the node struct.
 */
typedef void  *ipset_node_id;


/**
 * Nodes can either be terminal or nonterminal.
 */
enum ipset_node_type {
    IPSET_TERMINAL_NODE,
    IPSET_NONTERMINAL_NODE
};


/**
 * Return the type of node represented by a particular node ID.
 */
enum ipset_node_type
ipset_node_get_type(ipset_node_id node);


/**
 * Return the number of nodes that are reachable from the given node.
 * This does not include duplicates if a node is reachable via more
 * than one path.
 */
size_t
ipset_node_reachable_count(ipset_node_id node);


/**
 * Return the amount of memory used by the nodes in the given BDD.
 */
size_t
ipset_node_memory_size(ipset_node_id node);


/*-----------------------------------------------------------------------
 * Terminal nodes
 */

/**
 * Return the value of a terminal node.  The result is undefined if
 * the node ID represents a nonterminal.
 */
ipset_range
ipset_terminal_value(ipset_node_id node_id);


/*-----------------------------------------------------------------------
 * Nonterminal nodes
 */

/**
 * A nonterminal BDD node.  This is an inner node of the BDD tree.
 * The node represents one variable in an overall variable assignment.
 * The node has two children: a “low” child and a “high” child.  The
 * low child is the subtree that applies when the node's variable is
 * false or 0; the high child is the subtree that applies when it's
 * true or 1.
 *
 * This type does not take care of ensuring that all BDD nodes are
 * reduced; that is handled by the node_cache class.
 */
struct ipset_node {
    /** The variable that this node represents. */
    ipset_variable  variable;
    /** The subtree node for when the variable is false. */
    ipset_node_id  low;
    /** The subtree node for when the variable is true. */
    ipset_node_id  high;
};

/**
 * Return the node struct of a nonterminal node.  The result is
 * undefined if the node ID represents a terminal.
 */
struct ipset_node *
ipset_nonterminal_node(ipset_node_id node_id);

/**
 * Print out a node object.
 */
void
ipset_node_fprint(FILE *stream, struct ipset_node *node);


/*-----------------------------------------------------------------------
 * Node caches
 */

/**
 * A cache for BDD nodes.  By creating and retrieving nodes through
 * the cache, we ensure that a BDD is reduced.
 */
struct ipset_node_cache {
    /** A cache of the nonterminal nodes, keyed by their contents. */
    struct cork_hash_table  node_cache;
    /** A cache of the results of the AND operation. */
    struct cork_hash_table  and_cache;
    /** A cache of the results of the OR operation. */
    struct cork_hash_table  or_cache;
    /** A cache of the results of the ITE operation. */
    struct cork_hash_table  ite_cache;
};

/**
 * Convert between an index in the node vector, and the ID of the
 * corresponding nonterminal.  (Nonterminals have IDs < 0)
 */
size_t
ipset_node_id_to_index(ipset_node_id id);

/**
 * Convert between the ID of a nonterminal and its index in the node
 * vector.  (Nonterminals have IDs < 0)
 */
ipset_node_id
ipset_index_to_node_id(size_t index);

/**
 * Create a new node cache.
 */
struct ipset_node_cache *
ipset_node_cache_new();

/**
 * Free a node cache.
 */
void
ipset_node_cache_free(struct ipset_node_cache *cache);

/**
 * Create a new terminal node with the given value, returning its ID.
 * This function ensures that there is only one node with the given
 * value in this cache.
 */
ipset_node_id
ipset_node_cache_terminal(struct ipset_node_cache *cache, ipset_range value);

/**
 * Create a new nonterminal node with the given contents, returning
 * its ID.  This function ensures that there is only one node with the
 * given contents in this cache.
 */
ipset_node_id
ipset_node_cache_nonterminal(struct ipset_node_cache *cache,
                             ipset_variable variable,
                             ipset_node_id low, ipset_node_id high);


/**
 * Load a BDD from an input stream.  The error field is filled in with
 * an error condition is the BDD can't be read for any reason.
 */
ipset_node_id
ipset_node_cache_load(FILE *stream, struct ipset_node_cache *cache);


/**
 * Save a BDD to an output stream.  This encodes the set using only
 * those nodes that are reachable from the BDD's root node.
 */
int
ipset_node_cache_save(struct cork_stream_consumer *stream,
                      struct ipset_node_cache *cache, ipset_node_id node);


/**
 * Save a GraphViz dot graph for a BDD.  The graph script is written
 * to the given output stream.  This graph only includes those nodes
 * that are reachable from the BDD's root node.
 */
int
ipset_node_cache_save_dot(struct cork_stream_consumer *stream,
                          struct ipset_node_cache *cache, ipset_node_id node);


/*-----------------------------------------------------------------------
 * BDD operators
 */

/**
 * Calculate the logical AND (∧) of two BDDs.
 */
ipset_node_id
ipset_node_cache_and(struct ipset_node_cache *cache,
                     ipset_node_id lhs, ipset_node_id rhs);

/**
 * Calculate the logical OR (∨) of two BDDs.
 */
ipset_node_id
ipset_node_cache_or(struct ipset_node_cache *cache,
                    ipset_node_id lhs, ipset_node_id rhs);

/**
 * Calculate the IF-THEN-ELSE of three BDDs.  The first BDD should
 * only have 0 and 1 (FALSE and TRUE) in its range.
 */
ipset_node_id
ipset_node_cache_ite(struct ipset_node_cache *cache,
                     ipset_node_id f, ipset_node_id g, ipset_node_id h);


/*-----------------------------------------------------------------------
 * Evaluating BDDs
 */

/**
 * A function that provides the value for each variable in a BDD.
 */
typedef bool
(*ipset_assignment_func)(const void *user_data,
                         ipset_variable variable);

/**
 * An assignment function that gets the variable values from an array
 * of gbooleans.
 */
bool
ipset_bool_array_assignment(const void *user_data,
                            ipset_variable variable);

/**
 * An assignment function that gets the variable values from an array
 * of bits.
 */
bool
ipset_bit_array_assignment(const void *user_data,
                           ipset_variable variable);

/**
 * Evaluate a BDD given a particular assignment of variables.
 */
ipset_range
ipset_node_evaluate(ipset_node_id node,
                    ipset_assignment_func assignment,
                    const void *user_data);


/*-----------------------------------------------------------------------
 * Variable assignments
 */

/**
 * Each variable in the input to a Boolean function can be true or
 * false; it can also be EITHER, which means that the variable can be
 * either true or false in a particular assignment without affecting
 * the result of the function.
 */
enum ipset_tribool {
    IPSET_FALSE = 0,
    IPSET_TRUE = 1,
    IPSET_EITHER = 2
};


/**
 * An assignment is a mapping of variable numbers to Boolean values.
 * It represents an input to a Boolean function that maps to a
 * particular output value.  Each variable in the input to a Boolean
 * function can be true or false; it can also be EITHER, which means
 * that the variable can be either true or false in a particular
 * assignment without affecting the result of the function.
 */

struct ipset_assignment {
    /**
     * The underlying variable assignments are stored in a vector of
     * tribools.  Every variable that has a true or false value must
     * appear in the vector.  Variables that are EITHER only have to
     * appear to prevent gaps in the vector.  Any variables outside
     * the range of the vector are assumed to be EITHER.
     */
    cork_array(enum ipset_tribool)  values;
};


/**
 * Create a new assignment where all variables are indeterminite.
 */
struct ipset_assignment *
ipset_assignment_new();


/**
 * Free an assignment.
 */
void
ipset_assignment_free(struct ipset_assignment *assignment);


/**
 * Compare two assignments for equality.
 */
bool
ipset_assignment_equal(const struct ipset_assignment *assignment1,
                       const struct ipset_assignment *assignment2);


/**
 * Set the given variable, and all higher variables, to the EITHER
 * value.
 */
void
ipset_assignment_cut(struct ipset_assignment *assignment, ipset_variable var);


/**
 * Clear the assignment, setting all variables to the EITHER value.
 */
void
ipset_assignment_clear(struct ipset_assignment *assignment);


/**
 * Return the value assigned to a particular variable.
 */
enum ipset_tribool
ipset_assignment_get(struct ipset_assignment *assignment, ipset_variable var);


/**
 * Set the value assigned to a particular variable.
 */
void
ipset_assignment_set(struct ipset_assignment *assignment,
                     ipset_variable var, enum ipset_tribool value);


/*-----------------------------------------------------------------------
 * Expanded assignments
 */

/**
 * An iterator for expanding a variable assignment.  For each EITHER
 * variable in the assignment, the iterator yields a result with both
 * values.
 */
struct ipset_expanded_assignment {
    /** Whether there are any more assignments in this iterator. */
    bool finished;

    /**
     * The variable values in the current expanded assignment.  Since
     * there won't be any EITHERs in the expanded assignment, we can
     * use a byte array, and represent each variable by a single bit.
     */
    struct cork_buffer  values;

    /**
     * An array containing all of the variables that are EITHER in the
     * original assignment.
     */
    cork_array(ipset_variable)  eithers;
};


/**
 * Return an iterator that expands a variable assignment.  For each
 * variable that's EITHER in the assignment, the iterator yields a
 * result with both values.  The iterator will ensure that the
 * specified number of variables are given concrete values.
 */
struct ipset_expanded_assignment *
ipset_assignment_expand(const struct ipset_assignment *assignment,
                        ipset_variable var_count);


/**
 * Free an expanded assignment iterator.
 */
void
ipset_expanded_assignment_free(struct ipset_expanded_assignment *exp);


/**
 * Advance the iterator to the next assignment.
 */
void
ipset_expanded_assignment_advance(struct ipset_expanded_assignment *exp);


/*-----------------------------------------------------------------------
 * BDD iterators
 */

/**
 * An iterator for walking through the assignments for a given BDD
 * node.
 *
 * The iterator walks through each path in the BDD tree, stopping at
 * each terminal node.  Each time we reach a terminal node, we yield a
 * new ipset_assignment object representing the assignment of variables
 * along the current path.
 *
 * We maintain a stack of nodes leading to the current terminal, which
 * allows us to backtrack up the path to find the next terminal when
 * we increment the iterator.
 */
struct ipset_bdd_iterator {
    /** Whether there are any more assignments in this iterator. */
    bool finished;

    /**
     * The sequence of nonterminal nodes leading to the current
     * terminal.
     */
    cork_array(ipset_node_id)  stack;

    /** The current assignment. */
    struct ipset_assignment  *assignment;

    /**
     * The value of the BDD's function when applied to the current
     * assignment.
     */
    ipset_range  value;
};


/**
 * Return an iterator that yields all of the assignments in the given
 * BDD.  The iterator contains two items of interest.  The first is an
 * ipset_assignment providing the value that each variable takes, while
 * the second is the terminal value that is the result of the BDD's
 * function when applied to that variable assignment.
 */
struct ipset_bdd_iterator *
ipset_node_iterate(ipset_node_id root);


/**
 * Free a BDD iterator.
 */
void
ipset_bdd_iterator_free(struct ipset_bdd_iterator *iterator);


/**
 * Advance the iterator to the next assignment.
 */
void
ipset_bdd_iterator_advance(struct ipset_bdd_iterator *iterator);


#endif  /* IPSET_BDD_NODES_H */
