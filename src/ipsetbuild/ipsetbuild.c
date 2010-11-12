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


static gchar  *output_filename = NULL;


static GOptionEntry entries[] =
{
    { "output", 'o', 0, G_OPTION_ARG_FILENAME, &output_filename,
      "output file (\"-\" for stdout)", "FILE" },
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

    context = g_option_context_new("INPUT FILES");
    g_option_context_add_main_entries(context, entries, NULL);

    if (!g_option_context_parse(context, &argc, &argv, &error))
    {
        fprintf(stderr, "Error parsing command-line options: %s\n",
                error->message);
        exit(1);
    }

    /*
     * Verify that the user specified at least one SiLK file to read.
     */

    if (argc <= 1)
    {
        fprintf(stderr, "ERROR: You need to specify at "
                "least one input file.\n\n%s",
                g_option_context_get_help(context, TRUE, NULL));
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

    ip_set_t  set;
    ipset_init(&set);

    int  i;
    for (i = 1; i < argc; i++)
    {
        const char  *filename = argv[i];
        FILE  *stream;
        gboolean  close_stream;

        /*
         * Create a FILE object for the file.
         */

        if (strcmp(filename, "-") == 0)
        {
            fprintf(stderr, "Opening stdin...\n");
            filename = "stdin";

            stream = stdin;
            close_stream = FALSE;
        }

        else
        {
            fprintf(stderr, "Opening file %s...\n", filename);

            stream = fopen(filename, "rb");
            if (stream == NULL)
            {
                fprintf(stderr, "Cannot open file %s:\n  %s\n",
                        filename, strerror(errno));
                exit(1);
            }

            close_stream = TRUE;
        }

        /*
         * Read in one IP address per line in the file.
         */

        gsize  ip_count = 0;

#define MAX_LINELENGTH  4096

        gchar  line[MAX_LINELENGTH];

        while (fgets(line, MAX_LINELENGTH, stream) != NULL)
        {
            /*
             * Reserve enough space for an IPv6 address.
             */

            guint32  addr[4];
            int  rc;

            /*
             * Try to parse the line as an IPv4 address.  If that
             * works, add it to the set.
             */

            rc = inet_pton(AF_INET, line, addr);
            if (rc == 1)
            {
                ipset_ipv4_add(&set, addr);
                g_free(line);
                continue;
            }

            /*
             * If that didn't work, try IPv6.
             */

            rc = inet_pton(AF_INET6, line, addr);
            if (rc == 1)
            {
                ipset_ipv6_add(&set, addr);
                g_free(line);
                continue;
            }

            /*
             * Otherwise, we've got an error.
             */

            fprintf(stderr, "\"%s\" is not a valid IP address.\n", line);
            exit(1);
        }

        if (ferror(stream))
        {
            /*
             * There was an error reading from the stream.
             */

            fprintf(stderr, "Error reading from %s:\n  %s\n",
                    filename, strerror(errno));
            exit(1);
        }

        fprintf(stderr, "Read %" G_GSIZE_FORMAT " IP addresses from %s.\n",
                ip_count, filename);

        /*
         * Free the streams before opening the next file.
         */

        if (close_stream)
        {
            fclose(stream);
        }
    }

    fprintf(stderr, "Set uses %" G_GSIZE_FORMAT " bytes of memory.\n",
            ipset_memory_size(&set));

    /*
     * Serialize the IP set to the desired output file.
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

    if (!ipset_save(ostream, &set, &error))
    {
        fprintf(stderr, "Error saving IP set:\n  %s\n",
                error->message);
        exit(1);
    }

    /*
     * Close the output stream for exiting.
     */

    if (close_ostream)
    {
        fclose(ostream);
    }

    return 0;
}
