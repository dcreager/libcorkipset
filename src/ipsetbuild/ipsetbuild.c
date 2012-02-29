/* -*- coding: utf-8 -*-
 * ----------------------------------------------------------------------
 * Copyright © 2010-2012, RedJack, LLC.
 * All rights reserved.
 *
 * Please see the LICENSE.txt file in this distribution for license
 * details.
 * ----------------------------------------------------------------------
 */

#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libcork/core.h>

#include "ipset/ipset.h"


static char  *output_filename = NULL;

static struct option longopts[] = {
    { "output", required_argument, NULL, 'o' },
    { NULL, 0, NULL, 0 }
};

static void
usage(void)
{
    fprintf(stderr,
            "Usage: ipsetbuild [--output=<output file>]\n"
            "                  <IP file>\n");
}


int
main(int argc, char **argv)
{
    ipset_init_library();

    /* Parse the command-line options. */

    int  ch;
    while ((ch = getopt_long(argc, argv, "o:", longopts, NULL)) != -1) {
        switch (ch) {
            case 'o':
                output_filename = optarg;
                break;

            default:
                usage();
                exit(1);
        }
    }

    argc -= optind;
    argv += optind;

    /* Verify that the user specified at least one SiLK file to read. */

    if (argc == 0) {
        fprintf
            (stderr, "ERROR: You need to specify at least one input file.\n");
        usage();
        exit(1);
    }

    /* Read in the IP set files specified on the command line. */

    struct ip_set  set;
    ipset_init(&set);

    int  i;
    for (i = 0; i < argc; i++) {
        const char  *filename = argv[i];
        FILE  *stream;
        bool  close_stream;

        /* Create a FILE object for the file. */
        if (strcmp(filename, "-") == 0) {
            fprintf(stderr, "Opening stdin...\n");
            filename = "stdin";
            stream = stdin;
            close_stream = false;
        } else {
            fprintf(stderr, "Opening file %s...\n", filename);
            stream = fopen(filename, "rb");
            if (stream == NULL) {
                fprintf(stderr, "Cannot open file %s:\n  %s\n",
                        filename, strerror(errno));
                exit(1);
            }
            close_stream = true;
        }

        /* Read in one IP address per line in the file. */
        size_t  ip_count = 0;
        size_t  ip_count_v4 = 0;
        size_t  ip_count_v4_block = 0;
        size_t  ip_count_v6 = 0;
        size_t  ip_count_v6_block = 0;
        size_t  len;

#define MAX_LINELENGTH  4096
        char  line[MAX_LINELENGTH];
        char  *slash_pos;
        int   cidr;

        while (fgets(line, MAX_LINELENGTH, stream) != NULL) {
            struct cork_ip  addr;

            /* Set the trailing /n to 0 to support ip address checking */
            len = strlen(line);
            line[len-1] = 0;

            /* Check for a / indicating a CIDR block. Turn it into null and extract. */
            if ((slash_pos = strchr(line, '/'))) {
                *slash_pos = 0;
                slash_pos++;
                cidr = (int) strtol(slash_pos, (char **) NULL, 10);
            }

            /* Try to parse the line as an IP address. */
            if (cork_ip_init(&addr, line) != 0) {
                fprintf(stderr, "%s\n", cork_error_message());
                exit(1);
            }

            /* Add to address to the ipset and update the counters */
            (slash_pos == 0) ? ipset_ip_add(&set, &addr) : ipset_ip_add_network(&set, &addr, cidr);

            ip_count++;
            if (addr.version == 4) {
                (slash_pos == 0) ? ip_count_v4++ : ip_count_v4_block++;
            }
            else {
                (slash_pos == 0) ? ip_count_v6++ : ip_count_v6_block++;
            }
        }

        if (ferror(stream)) {
            /* There was an error reading from the stream. */
            fprintf(stderr, "Error reading from %s:\n  %s\n",
                    filename, strerror(errno));
            exit(1);
        }

        fprintf(stderr, "Read %zu IP address records from %s.\n", ip_count, filename);
        fprintf(stderr, "  IPv4: %zu addresses, %zu block%s\n", ip_count_v4, 
                ip_count_v4_block, (ip_count_v4_block == 1 ? "" : "s")); 
        fprintf(stderr, "  IPv6: %zu addresses, %zu block%s\n", ip_count_v6, 
                ip_count_v6_block, (ip_count_v6_block == 1 ? "" : "s"));

        /* Free the streams before opening the next file. */
        if (close_stream) {
            fclose(stream);
        }
    }

    fprintf(stderr, "Set uses %zu bytes of memory.\n",
            ipset_memory_size(&set));

    /* Serialize the IP set to the desired output file. */
    FILE  *ostream;
    bool  close_ostream;

    if ((output_filename == NULL) || (strcmp(output_filename, "-") == 0)) {
        fprintf(stderr, "Writing to stdout...\n");
        ostream = stdout;
        output_filename = "stdout";
        close_ostream = false;
    } else {
        fprintf(stderr, "Writing to file %s...\n", output_filename);
        ostream = fopen(output_filename, "wb");
        if (ostream == NULL) {
            fprintf(stderr, "Cannot open file %s:\n  %s\n",
                    output_filename, strerror(errno));
            exit(1);
        }
        close_ostream = true;
    }

    if (ipset_save(ostream, &set) != 0) {
        fprintf(stderr, "Error saving IP set:\n  %s\n",
                cork_error_message());
        exit(1);
    }

    /* Close the output stream for exiting. */
    if (close_ostream) {
        fclose(ostream);
    }

    return 0;
}
