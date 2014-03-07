/* -*- coding: utf-8 -*-
 * ----------------------------------------------------------------------
 * Copyright © 2014, RedJack, LLC.
 * All rights reserved.
 *
 * Please see the LICENSE.txt file in this distribution for license
 * details.
 * ----------------------------------------------------------------------
 */

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libcork/core.h>
#include <libcork/helpers/errors.h>

#include "ipset/bdd/nodes.h"
#include "ipset/errors.h"
#include "ipset/ipset.h"


struct ip_removal {
    size_t  line;
    struct cork_ip  address;
    unsigned int  cidr;
    bool  has_cidr;
};

static bool
is_whitespace(const char *str)
{
    while (*str) {
        if (isspace(*str) == 0) {
            return false;
        }
        str++;
    }
    return true;
}

#define LINE_LENGTH  128

struct ip_set *
ipset_read_text_file(const char *filename)
{
    struct ip_set  *ipset;
    struct ip_removal  *entry;
    cork_array(struct ip_removal)  removals;
    FILE  *in_stream;
    char  in_line[LINE_LENGTH];
    char  *slash_pos;
    size_t  line_num = 0;
    unsigned int  cidr = 0;
    bool  set_unchanged = false;
    size_t  removal_count;

    ipset = ipset_new();
    ipset_init(ipset);
    cork_array_init(&removals);
    rpp_check(in_stream = fopen(filename, "rb"));
    while (fgets(in_line, LINE_LENGTH, in_stream) != NULL) {
        char  *addr_str;
        struct cork_ip  addr;
        bool  remove_ip;

        line_num++;

        /* Skip empty lines and comments. Comments start with '#'
         * in the first column. */
        if ((in_line[0] == '#') || (is_whitespace(in_line))) {
            continue;
        }

        /* Check for a negating IP address.  If so, then the IP
         * address starts just after the '!'. */
        if (in_line[0] == '!') {
            remove_ip = true;
            addr_str = in_line + 1;
        } else {
            remove_ip = false;
            addr_str = in_line;
        }

        /* Chomp the trailing newline so we don't confuse our IP
         * address parser. */
        addr_str[strlen(addr_str) - 1] = '\0';

        /* Check for a / indicating a CIDR block.  If one is
         * present, split the string there and parse the trailing
         * part as a CIDR prefix integer. */
        if ((slash_pos = strchr(addr_str, '/')) != NULL) {
            char  *endptr;
            *slash_pos = '\0';
            slash_pos++;
            cidr = (unsigned int) strtol(slash_pos, &endptr, 10);
            if (endptr == slash_pos) {
                fprintf(stderr, "Error: Line %zu: Missing CIDR prefix\n",
                        line_num);
                continue;
            } else if (*slash_pos == '\0' || *endptr != '\0') {
                fprintf(stderr, "Error: Line %zu: Invalid CIDR prefix \"%s\"\n",
                          line_num, slash_pos);
                continue;
            }
        }

        /* Try to parse the line as an IP address. */
        if (cork_ip_init(&addr, addr_str) != 0) {
            fprintf(stderr, "Error: Line %zu: %s\n",
                    line_num, cork_error_message());
            continue;
        }

        /* Add the address to the ipset */
        if (slash_pos == NULL) {
            /* This is a regular (non-CIDR) address */
            if (remove_ip) {
                entry = cork_array_append_get(&removals);
                entry->line = line_num;
                entry->address = addr;
                entry->cidr = 0;
                entry->has_cidr = false;
            } else {
                set_unchanged = ipset_ip_add(ipset, &addr);
                if (set_unchanged) {
                    fprintf(stderr, "Alert: %s, line %zu: %s is a duplicate\n",
                            filename, line_num, addr_str);
                }
            }
            if (cork_error_occurred()) {
                fprintf(stderr, "Error: %s, line %zu: Bad IP address: "
                                "\"%s\": %s\n",
                                filename, line_num, addr_str,
                                cork_error_message());
                cork_error_clear();
                continue;
            }
        } else {
            /* This is an address with a CIDR block. */
            if (!cork_ip_is_valid_network(&addr, cidr)) {
                fprintf(stderr, "Error: %s, line %zu: Bad CIDR block: "
                                "\"%s/%u\"\n",
                                filename, line_num, addr_str, cidr);
                continue;
            }
            if (remove_ip) {
                entry = cork_array_append_get(&removals);
                entry->line = line_num;
                entry->address = addr;
                entry->cidr = cidr;
                entry->has_cidr = true;
            } else {
                set_unchanged = ipset_ip_add_network(ipset, &addr, cidr);
            }
            if (cork_error_occurred()) {
                fprintf(stderr, "Error: %s, line %zu: Bad IP address: "
                                "\"%s/%u\": %s\n",
                                filename, line_num, addr_str, cidr,
                                cork_error_message());
                cork_error_clear();
                continue;
            }
            if (!remove_ip) {
                if (set_unchanged) {
                    fprintf(stderr, "Alert: %s, line %zu: %s/%u is a "
                                    "duplicate\n",
                                    filename, line_num, addr_str, cidr);
                }
            }
        }
    }

    /* Combine the removals array with the set */
    removal_count = cork_array_size(&removals);
    for (int i = 0; i < removal_count; i++) {
        entry = &cork_array_at(&removals, i);
        if (entry->has_cidr == true) {
            set_unchanged = ipset_ip_remove_network
                            (ipset, &entry->address, entry->cidr);
            if (set_unchanged) {
                char  ip_buf[CORK_IP_STRING_LENGTH];
                cork_ip_to_raw_string(&entry->address, ip_buf);
                fprintf(stderr, "Alert: line %zu: %s/%u is not in the set\n",
                        entry->line, ip_buf, entry->cidr);
            }
        } else {
            set_unchanged = ipset_ip_remove(ipset, &entry->address);
            if (set_unchanged) {
                char  ip_buf[CORK_IP_STRING_LENGTH];
                cork_ip_to_raw_string(&entry->address, ip_buf);
                fprintf(stderr, "Alert: line %zu: %s is not in the set\n",
                                entry->line, ip_buf);
           }
        }
    }
    cork_array_done(&removals);
    return ipset;
}
