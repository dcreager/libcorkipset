/* -*- coding: utf-8 -*-
 * ----------------------------------------------------------------------
 * Copyright © 2010, RedJack, LLC.
 * All rights reserved.
 *
 * Please see the LICENSE.txt file in this distribution for license
 * details.
 * ----------------------------------------------------------------------
 */


#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <glib.h>

#include <ipset/ipset.h>


static gchar  *input_filename = "-";
static gchar  *output_filename = "-";
static gboolean  want_networks = FALSE;


static GOptionEntry entries[] =
{
    { "input", 'i', 0,
      G_OPTION_ARG_FILENAME, &input_filename,
      "input files (\"-\" for stdin)", "INPUT FILES" },
    { "output", 'o', 0,
      G_OPTION_ARG_FILENAME, &output_filename,
      "output file (\"-\" for stdout)", "FILE" },
    { "networks", 'n', 0,
      G_OPTION_ARG_NONE, &want_networks,
      "print out CIDR network blocks", NULL },
    { NULL }
};


/**
 * A log handler that ignores the logging messages.
 */

static void
ignore_log_message(const gchar *log_domain, GLogLevelFlags log_level,
                   const gchar *message, gpointer user_data)
{
}


int
main(int argc, char **argv)
{
    ipset_init_library();

    /*
     * Parse the command-line options.
     */

    GError  *error = NULL;
    GOptionContext  *context;

    context = g_option_context_new("");
    g_option_context_add_main_entries(context, entries, NULL);

    if (!g_option_context_parse(context, &argc, &argv, &error))
    {
        fprintf(stderr, "Error parsing command-line options: %s\n",
                error->message);
        exit(1);
    }

    /*
     * Set up logging.
     */

    g_log_set_handler("ipset", G_LOG_LEVEL_DEBUG,
                      ignore_log_message, NULL);

    /*
     * Read in the IP set files specified on the command line.
     */

    ip_set_t  *set = NULL;

    {
        FILE  *stream;
        gboolean  close_stream;

        /*
         * Create a FILE object for the file.
         */

        if (strcmp(input_filename, "-") == 0)
        {
            fprintf(stderr, "Opening stdin...\n");
            input_filename = "stdin";

            stream = stdin;
            close_stream = FALSE;
        }

        else
        {
            fprintf(stderr, "Opening file %s...\n", input_filename);

            stream = fopen(input_filename, "rb");
            if (stream == NULL)
            {
                fprintf(stderr, "Cannot open file %s:\n  %s\n",
                        input_filename, strerror(errno));
                exit(1);
            }

            close_stream = TRUE;
        }

        /*
         * Read in the IP set from the specified file.
         */

        set = ipset_load(stream, &error);

        if (set == NULL)
        {
            fprintf(stderr, "Error reading %s:\n  %s\n",
                    input_filename, error->message);
            exit(1);
        }
    }

    /*
     * Print out the IP addresses in the set.
     */

    FILE  *ostream;
    gboolean  close_ostream;

    if ((output_filename == NULL) ||
        (strcmp(output_filename, "-") == 0))
    {
        fprintf(stderr, "Writing to stdout...\n");

        ostream = stdout;
        output_filename = "stdout";
        close_ostream = FALSE;
    }

    else
    {
        fprintf(stderr, "Writing to file %s...\n", output_filename);

        ostream = fopen(output_filename, "wb");
        if (ostream == NULL)
        {
            fprintf(stderr, "Cannot open file %s:\n  %s\n",
                    output_filename, strerror(errno));
            exit(1);
        }

        close_ostream = TRUE;
    }

    GString *str = g_string_new(NULL);

    if (want_networks)
    {
        /*
         * If requested, iterate through network blocks instead of
         * individual IP addresses.
         */

        ipset_iterator_t  *it;
        for (it = ipset_iterate_networks(set, TRUE);
             !it->finished;
             ipset_iterator_advance(it))
        {
            g_string_printf(str, "%s/%u\n",
                            ipset_ip_to_string(&it->addr),
                            it->netmask);

            if (fputs(str->str, ostream) == EOF)
            {
                fprintf(stderr, "Cannot write to file %s:\n  %s\n",
                        output_filename, strerror(errno));
                exit(1);
            }
        }

    }

    else
    {
        /*
         * The user wants individual IP addresses.  Hope they know
         * what they're doing!
         */

        ipset_iterator_t  *it;
        for (it = ipset_iterate(set, TRUE);
             !it->finished;
             ipset_iterator_advance(it))
        {
            g_string_printf(str, "%s\n",
                            ipset_ip_to_string(&it->addr));

            if (fputs(str->str, ostream) == EOF)
            {
                fprintf(stderr, "Cannot write to file %s:\n  %s\n",
                        output_filename, strerror(errno));
                exit(1);
            }
        }
    }

    g_string_free(str, TRUE);
    ipset_free(set);

    /*
     * Close the output stream for exiting.
     */

    if (close_ostream)
    {
        fclose(ostream);
    }

    return 0;
}
