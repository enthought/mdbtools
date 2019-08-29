/* MDB Tools - A library for reading MS Access database file
 * Copyright (C) 2000 Brian Bruns
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#include "string.h"
#include "mdbtools.h"

#ifdef DMALLOC
#include "dmalloc.h"
#endif

#undef MDB_BIND_SIZE
#define MDB_BIND_SIZE 200000

#define is_quote_type(x) (x==MDB_TEXT || x==MDB_OLE || x==MDB_MEMO || x==MDB_DATETIME || x==MDB_BINARY || x==MDB_REPID)
#define is_binary_type(x) (x==MDB_OLE || x==MDB_BINARY || x==MDB_REPID)

int
main(int argc, char **argv)
{
	unsigned int i;
	MdbHandle *mdb;
	MdbTableDef *table;
	MdbColumn *col;
	char **bound_values;
	int  *bound_lens; 
	FILE *outfile = stdout;
	char *delimiter = NULL;
	char *row_delimiter = NULL;
	char *quote_char = NULL;
	char *escape_char = NULL;
	int header_row = 1;
	char *insert_dialect = NULL;
	char *date_fmt = NULL;
	char *namespace = NULL;
	char *str_bin_mode = NULL;
	char *value;
	size_t length;

	#ifdef _WIN32
	setmode(fileno(stdout), O_BINARY);
	#endif

	GOptionContext *opt_context;

	opt_context = g_option_context_new("<file> <table> - export data from MDB file as raw");
	if (argc != 3) {
		fputs("Wrong number of arguments.\n\n", stderr);
		fputs(g_option_context_get_help(opt_context, TRUE, NULL), stderr);
		exit(1);
	}

	/* Open file */
	if (!(mdb = mdb_open(argv[1], MDB_NOFLAGS))) {
		/* Don't bother clean up memory before exit */
		exit(1);
	}

	table = mdb_read_table_by_name(mdb, argv[2], MDB_TABLE);
	if (!table) {
		fprintf(stderr, "Error: Table %s does not exist in this database.\n", argv[2]);
		/* Don't bother clean up memory before exit */
		exit(1);
	}

	/* read table */
	mdb_read_columns(table);
	mdb_rewind_table(table);

	bound_values = (char **) g_malloc(table->num_cols * sizeof(char *));
	bound_lens = (int *) g_malloc(table->num_cols * sizeof(int));
	for (i=0;i<table->num_cols;i++) {
		/* bind columns */
		bound_values[i] = (char *) g_malloc0(MDB_BIND_SIZE);
		mdb_bind_column(table, i+1, bound_values[i], &bound_lens[i]);
	}

        const unsigned int null_size = 0;
        const unsigned int int_size  = sizeof(unsigned int);
        const unsigned int char_size  = sizeof(unsigned char);
        const unsigned int short_size = sizeof(unsigned short);
        const unsigned int float_size = sizeof(float);
        const unsigned int double_size = sizeof(double);

        /* Write out the number of columns and rows */
        fwrite(&(table->num_rows), int_size, 1, outfile);
        fwrite(&(table->num_cols), int_size, 1, outfile);
        
	if (header_row) {
		for (i=0; i<table->num_cols; i++) {
			col=g_ptr_array_index(table->columns,i);
                        fwrite(&(col->col_type), int_size, 1, outfile);
                        unsigned int str_size = (unsigned int) strlen(col->name);
                        fwrite(&str_size, int_size, 1, outfile);
                        fwrite(&(col->name), str_size, 1, outfile);
		}

	}

	while(mdb_fetch_row(table)) {

		for (i=0;i<table->num_cols;i++) {
			col=g_ptr_array_index(table->columns,i);
			if (!bound_lens[i]) {
                            fwrite(&null_size, int_size, 1, outfile);
                            continue;
                        }

                        switch (col->col_type) {
                            case MDB_OLE:
                            {
                                value = mdb_ole_read_full(mdb, col, &length);
                                fwrite(&length, int_size, 1, outfile);
                                fwrite(value, length, 1, outfile);
                                free(value);
                                break;
                            }
                            
                            case MDB_BINARY: case MDB_TEXT: case MDB_MEMO:
                            {
                                unsigned int str_size = (unsigned int) strlen(bound_values[i]);
                                fwrite(&str_size, int_size, 1, outfile);
                                fwrite(bound_values[i], str_size, 1, outfile);
                                break;
                            }

                            case MDB_BYTE: case MDB_BOOL:
                            {
                                char byte = (char) strtol(bound_values[i], NULL, 10);
                                fwrite(&char_size, int_size, 1, outfile);
                                fwrite(&byte, char_size, 1, outfile);
                                break;
                            }   

                            case MDB_INT:
                            {
                                short val = (short) strtol(bound_values[i], NULL, 10);
                                fwrite(&short_size, int_size, 1, outfile);
                                fwrite(&val, short_size, 1, outfile);
                                break;
                            }

                            case MDB_LONGINT:
                            {
                                long val = (long) strtol(bound_values[i], NULL, 10);
                                fwrite(&int_size, int_size, 1, outfile);
                                fwrite(&val, int_size, 1, outfile);
                                break;
                            }

                            case MDB_FLOAT:
                            {
                                float val = strtof(bound_values[i], NULL);
                                fwrite(&float_size, int_size, 1, outfile);
                                fwrite(&val, float_size, 1, outfile);
                                break;
                            }

                            case MDB_DOUBLE:
                            {
                                double val = strtod(bound_values[i], NULL);
                                fwrite(&double_size, int_size, 1, outfile);
                                fwrite(&val, double_size, 1, outfile);
                                break;
                            }
                            
                            default:
                                fprintf(stderr, "Unhandled data type %i for %s", col->col_type, col->name);
                                
                        }
		}
	}
	
	/* free the memory used to bind */
	for (i=0;i<table->num_cols;i++) {
		g_free(bound_values[i]);
	}
	g_free(bound_values);
	g_free(bound_lens);
	mdb_free_tabledef(table);

	mdb_close(mdb);
	g_option_context_free(opt_context);

	// g_free ignores NULL
	g_free(quote_char);
	g_free(delimiter);
	g_free(row_delimiter);
	g_free(insert_dialect);
	g_free(date_fmt);
	g_free(escape_char);
	g_free(namespace);
	g_free(str_bin_mode);
	return 0;
}
