/* -*- coding: utf-8 -*-
 * ----------------------------------------------------------------------
 * Copyright © 2010-2012, RedJack, LLC.
 * All rights reserved.
 *
 * Please see the LICENSE.txt file in this distribution for license
 * details.
 * ----------------------------------------------------------------------
 */

#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libcork/core.h>

#include "ipset/ipset.h"


static char  *output_filename = NULL;
static bool  loose_cidr = false;
static int  verbosity = 0;

static struct option longopts[] = {
    { "help", no_argument, NULL, 'h' },
    { "output", required_argument, NULL, 'o' },
    { "loose-cidr", 0, NULL, 'l' },
    { "verbose", 0, NULL, 'v' },
    { "quiet", 0, NULL, 'q' },
    { NULL, 0, NULL, 0 }
};

static bool
is_string_whitespace(const char *str)
{
    while (*str) {
        if (isspace(*str) == 0) {
            return false;
        }
        str++;
    }
    return true;
}

#define USAGE \
"Usage: ipsetbuild [options] <input file>...\n"

#define FULL_USAGE \
USAGE \
"\n" \
"Constructs a binary IP set file from a list of IP addresses and networks.\n" \
"\n" \
"Options:\n" \
"  <input file>...\n" \
"    A list of text files that contain the IP addresses and networks to add\n" \
"    to the set.  To read from stdin, use \"-\" as the filename.\n" \
"  --output=<filename>, -o <filename>\n" \
"    Writes the binary IP set file to <filename>.  If this option isn't\n" \
"    given, then the binary set will be written to standard output.\n" \
"  --loose-cidr, -l\n" \
"    Be more lenient about the address portion of any CIDR network blocks\n" \
"    found in the input file.\n" \
"  --verbose, -v\n" \
"    Show summary information about the IP set that's built, as well as\n" \
"    progress information about the files being read and written.  If this\n" \
"    option is not given, the only output will be any error, alert, or\n" \
"    warning messages that occur.\n" \
"  --quiet, -q\n" \
"    Show only error message for malformed output. All warnings, alerts,\n" \
"    and summary information about the IP set is suppressed.\n" \
"  --help\n" \
"    Display this help and exit.\n" \
"\n" \
"Input format:\n" \
"  Each input file must contain one IP address or network per line.  Lines\n" \
"  beginning with a \"#\" are considered comments and are ignored.  Each\n" \
"  IP address must have one of the following formats:\n" \
"\n" \
"    x.x.x.x\n" \
"    x.x.x.x/cidr\n" \
"    xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:xxxx\n" \
"    xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:xxxx/cidr\n" \
"\n" \
"    The first two are for IPv4 addresses and networks; the second two for\n" \
"    IPv6 addresses and networks.  For IPv6 addresses, you can use the \"::\"\n" \
"    shorthand notation to collapse consecutive \"0\" portions.\n" \
"\n" \
"    If an address contains a \"/cidr\" suffix, then the entire CIDR network\n" \
"    of addresses will be added to the set.  You must ensure that the low-\n" \
"    order bits of the address are set to 0; if not, we'll raise an error.\n" \
"    (If you pass in the \"--loose-cidr\" option, we won't perform this\n" \
"    sanity check.)\n"


int
main(int argc, char **argv)
{
    ipset_init_library();

    /* Parse the command-line options. */

    int  ch;
    while ((ch = getopt_long(argc, argv, "hlo:vq", longopts, NULL)) != -1) {
        switch (ch) {
            case 'h':
                fprintf(stdout, FULL_USAGE);
                exit(0);

            case 'l':
                loose_cidr = true;
                break;

            case 'o':
                output_filename = optarg;
                break;

            case 'v':
                verbosity++;
                break;

            case 'q':
                verbosity--;
                break;

            default:
                fprintf(stderr, USAGE);
                exit(1);
        }
    }

    argc -= optind;
    argv += optind;

    /* Verify that the user specified at least one text file to read. */
    if (argc == 0) {
        fprintf(stderr,
                "ipsetbuild: You need to specify at least one input file.\n");
        fprintf(stderr, USAGE);
        exit(1);
    }

    /* And an output file to write to. */
    if (output_filename == NULL) {
        fprintf(stderr,
                "ipsetbuild: You need to specify an output file.\n");
        fprintf(stderr, USAGE);
        exit(1);
    }

    /* Read in the IP set files specified on the command line. */

    bool  read_from_stdin = false;
    struct ip_set  set;
    ipset_init(&set);

    int  i;
    for (i = 0; i < argc; i++) {
        const char  *filename = argv[i];
        FILE  *stream;
        bool  close_stream;

        /* Create a FILE object for the file. */
        if (strcmp(filename, "-") == 0) {
            if (read_from_stdin) {
                fprintf(stderr,
                        "ipsetbuild: Cannot read from stdin more than once.\n");
                exit(1);
            }

            if (verbosity > 0) {
                fprintf(stderr, "Opening stdin...\n\n");
            }
            filename = "stdin";
            stream = stdin;
            close_stream = false;
            read_from_stdin = true;
        } else {
            if (verbosity > 0) {
                fprintf(stderr, "Opening file %s...\n", filename);
            }
            stream = fopen(filename, "rb");
            if (stream == NULL) {
                fprintf(stderr, "ipsetbuild: Cannot open file %s:\n  %s\n",
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
        size_t  line_num = 0;
        size_t  ip_error_num = 0;
        bool  ip_error = false;

#define MAX_LINELENGTH  4096
        char  line[MAX_LINELENGTH];
        char  *slash_pos;
        unsigned int  cidr = 0;

        while (fgets(line, MAX_LINELENGTH, stream) != NULL) {
            struct cork_ip  addr;
            bool  remove_ip = false;

            line_num++;

            /* Skip empty lines and comments. Comments start with '#'
             * in the first column. */
            if ((line[0] == '#') || (is_string_whitespace(line))) {
                continue;
            }

            /* Check for a negating IP address. If so, then shift the
             * characters in `line` one position to the left. */
            if (line[0] == '!') {
                remove_ip = true;
                size_t  len = strlen(line);
                int  i;
                for (i = 0; i < len-1; i++) {
                    line[i] = line[i+1];
                }
                line[len-1] = '\0';
            }

            /* Chomp the trailing newline so we don't confuse our IP
             * address parser. */
            size_t  len = strlen(line);
            line[len-1] = '\0';

            /* Check for a / indicating a CIDR block.  If one is
             * present, split the string there and parse the trailing
             * part as a CIDR prefix integer. */
            if ((slash_pos = strchr(line, '/')) != NULL) {
                char  *endptr;
                *slash_pos = '\0';
                slash_pos++;
                cidr = (unsigned int) strtol(slash_pos, &endptr, 10);
                if (endptr == slash_pos) {
                    fprintf(stderr,
                            "Error: Line %zu: Missing CIDR prefix\n",
                            line_num);
                    ip_error_num++;
                    ip_error = true;
                    continue;
                } else if (*slash_pos == '\0' || *endptr != '\0') {
                    fprintf(stderr,
                            "Error: Line %zu: Invalid CIDR prefix \"%s\"\n",
                            line_num, slash_pos);
                    ip_error_num++;
                    ip_error = true;
                    continue;
                }
            }

            /* Try to parse the line as an IP address. */
            if (cork_ip_init(&addr, line) != 0) {
                fprintf(stderr, "Error: Line %zu: %s\n",
                        line_num, cork_error_message());
                cork_error_clear();
                ip_error_num++;
                ip_error = true;
                continue;
            }

            /* Add to address to the ipset and update the counters */
            bool  set_unchanged;
            if (slash_pos == NULL) {
                if (remove_ip) {
                    set_unchanged = ipset_ip_remove(&set, &addr);
                    if (set_unchanged) {
                        if (verbosity >= 0) {
                            fprintf(stderr,
                                    "Alert: Line %zu: %s is not in the set\n",
                                    line_num, line);
                        }
                    } else {
                        if (addr.version == 4) {
                            ip_count_v4--;
                        } else {
                            ip_count_v6--;
                        }
                    }
                } else {
                    set_unchanged = ipset_ip_add(&set, &addr);
                    if (set_unchanged) {
                        if (verbosity >= 0) {
                            fprintf(stderr,
                                    "Alert: Line %zu: %s is a duplicate\n",
                                    line_num, line);
                        }
                        ip_count++;
                    } else {
                        if (addr.version == 4) {
                            ip_count_v4++;
                        } else {
                            ip_count_v6++;
                        }
                    }
                }
            } else {
                /* If loose-cidr was not a command line option, then check the
                 * alignment of the IP address with the CIDR block. */
                if (!loose_cidr) {
                    if (!cork_ip_is_valid_network(&addr, cidr)) {
                        fprintf(stderr, "Error: Line %zu: Bad CIDR block: "
                                "\"%s/%u\"\n",
                                line_num, line, cidr);
                        ip_error_num++;
                        ip_error = true;
                        continue;
                    }
                }
                if (remove_ip) {
                    set_unchanged = ipset_ip_remove_network(&set, &addr, cidr);
                } else {
                    set_unchanged = ipset_ip_add_network(&set, &addr, cidr);
                }
                if (cork_error_occurred()) {
                    fprintf(stderr, "Error: Line %zu: Invalid IP address: "
                            "\"%s/%u\": %s\n",
                            line_num, line, cidr, cork_error_message());
                    cork_error_clear();
                    ip_error_num++;
                    ip_error = true;
                    continue;
                }
                if (remove_ip) {
                    if (set_unchanged) {
                        if (verbosity >= 0) {
                            fprintf(stderr,
                                   "Alert: Line %zu: %s/%u is not in the set\n",
                                    line_num, line, cidr);
                        }
                    } else {
                        if (addr.version == 4) {
                            ip_count_v4_block--;
                        } else {
                            ip_count_v6_block--;
                        }
                    }
                } else {
                    if (set_unchanged) {
                        if (verbosity >= 0) {
                            fprintf(stderr,
                                    "Alert: Line %zu: %s/%u is a duplicate\n",
                                    line_num, line, cidr);
                        }
                        ip_count++;
                    } else {
                        if (addr.version == 4) {
                            ip_count_v4_block++;
                        } else {
                            ip_count_v6_block++;
                        }
                    }
                }
            }
        }

        if (ferror(stream)) {
            /* There was an error reading from the stream. */
            fprintf(stderr, "Error reading from %s:\n  %s\n",
                    filename, strerror(errno));
            exit(1);
        }

        if (verbosity > 0) {
            fprintf(stderr,
                    "Summary: Read %zu valid IP address records from %s.\n",
                    ip_count, filename);
            fprintf(stderr, "  IPv4: %zu addresses, %zu block%s\n", ip_count_v4,
                    ip_count_v4_block, (ip_count_v4_block == 1)? "": "s");
            fprintf(stderr, "  IPv6: %zu addresses, %zu block%s\n", ip_count_v6,
                    ip_count_v6_block, (ip_count_v6_block == 1)? "": "s");
        }

        /* Free the streams before opening the next file. */
        if (close_stream) {
            fclose(stream);
        }

        /* If the input file has errors, then terminate the program. */
        if (ip_error) {
            fprintf(stderr, "The program halted with %zu input error%s.\n",
                    ip_error_num, (ip_error_num == 1)? "": "s");
            exit(1);
        }
    }

    if (verbosity > 0) {
        fprintf(stderr, "Set uses %zu bytes of memory.\n",
                ipset_memory_size(&set));
    }

    /* Serialize the IP set to the desired output file. */
    FILE  *ostream;
    bool  close_ostream;

    if (strcmp(output_filename, "-") == 0) {
        if (verbosity > 0) {
            fprintf(stderr, "Writing to stdout...\n");
        }
        ostream = stdout;
        output_filename = "stdout";
        close_ostream = false;
    } else {
        if (verbosity > 0) {
            fprintf(stderr, "Writing to file %s...\n", output_filename);
        }
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
