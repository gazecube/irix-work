#ident	"include/parser.h:  $Revision"
#ifndef PARSER_H
#define PARSER_H
/*
 * parser.h -- definitions for command parser routines
 */

#include <sgidefs.h>

struct range {
	__psint_t	ra_base;
	__psint_t	ra_count;
	int		ra_size;
};

struct cmd_table {
	char	*ct_string;
	int	(*ct_routine)(int, char *[], char *[], struct cmd_table *);
	char	*ct_usage;
};

#define CT_ROUTINE	(int (*)(int, char *[], char *[], struct cmd_table *))
#define	LINESIZE	256
#define EDIT_HISTORY	10
struct edit_linebuf {
	char	lines[EDIT_HISTORY][LINESIZE];
	int	current;
};

void command_parser(struct cmd_table *, char *, int, struct cmd_table *);
struct cmd_table *lookup_cmd(struct cmd_table *, char *);
int parse_range(register char *, int, struct range *);
int help(int, char **, char **, struct cmd_table *);
char *edit_gets(struct edit_linebuf *);
void usage(struct cmd_table *);
void parse_error(char *);

#endif /* PARSER_H */
