 #ident "$Revision: 1.59 $"

/*
 * parser.c -- command parser
 */

#include <sys/debug.h>
#include <saioctl.h>
#include <parser.h>
#include <setjmp.h>
#include <stringlist.h>
#include <ctype.h>
#include <arcs/io.h>
#include <arcs/errno.h>
#include <libsc.h>
#include <libsc_internal.h>

#define	streq(x,y)	(strcmp(x, y) == 0)

struct cmd_table *lookup_cmd(struct cmd_table *, char *);
static char *get_word(char **);
extern jmp_buf perr_buf;

void
command_parser(struct cmd_table *cmd_table, char *prompt, int search_path,
	struct cmd_table *parent_table)
{
	register struct cmd_table *ct;
	static struct edit_linebuf elinebuf;
	char *linebuf;
	struct string_list argv;
	LONG err;
	int argc;

	for (;;) {
		setjmp(perr_buf);
		ioctl(StandardIn,TIOCSETXOFF,1);
		printf("%s", prompt);
		if ((linebuf = edit_gets(&elinebuf)) == 0)
			return;
	        argc = _argvize(expand(linebuf, 0), &argv);
		if (argc == 0)
			continue;

		ct = lookup_cmd(cmd_table, argv.strptrs[0]);
		if (!ct && parent_table)
			ct = lookup_cmd(parent_table, argv.strptrs[0]);
		if (!ct) {
			if (search_path) {
				setenv ("kernname", argv.strptrs[0]);
				if (err = Execute(argv.strptrs[0],
						(LONG)argv.strcnt,
						argv.strptrs,environ))
                                                if(err == ENOENT)
						printf("Unable to execute %s: %s\n",
						argv.strptrs[0],
						arcs_strerror(err));
				continue;
			}
			printf("%s: Command not found.\n", argv.strptrs[0]);
			continue;
		}
		if ((*ct->ct_routine)(argc, argv.strptrs, environ, cmd_table))
			usage(ct);
	}
}

int
_argvize(char *linebuf, struct string_list *slp)
{
	register int argc;
	register char *word;

	init_str(slp);
	for (argc = 0; *linebuf; argc++) {
		if((word = get_word(&linebuf)) == NULL)
			return 0;
		new_str1(word, slp);
	}
	return(argc);
}

static char *
get_word(char **cpp)
{
	static char word[LINESIZE];
	register char *wp;
	register char c;
	char *cp;

	cp = *cpp;
	while (isspace(*cp))
		cp++;
	wp = word;
	while ((c = *cp) && !isspace(c) && wp < &word[LINESIZE-1]) {
		switch (c) {
		case '"':
		case '\'':
			cp++;
			while (*cp && *cp != c && wp < &word[LINESIZE-2])
				*wp++ = *cp++;
			if (*cp == c)
				cp++;
			break;
		default:
			*wp++ = *cp++;
			break;
		}
	}
	while (isspace(*cp))
		cp++;
	*cpp = cp;
	*wp = 0;
	if (wp == &word[LINESIZE-1])
		parse_error("Line too long");
	else if (wp == word)
		return(NULL);
	return(word);
}

struct cmd_table *
lookup_cmd(struct cmd_table *cmd_table, char *cp)
{
	register struct cmd_table *ct;
	char *sp;

	for (ct = cmd_table; ct->ct_string; ct++) {
		sp = ct->ct_string;
		if (*sp == '.')
		    sp++;
		if (streq(sp, cp))
			return(ct);
	}
	return((struct cmd_table *)0);
}

void
usage(struct cmd_table *ct)
{
	printf("Usage: %s\n", ct->ct_usage);
}

int
help(int argc, char **argv, char **envp, struct cmd_table *cmd_table)
{
	register struct cmd_table *ct;

	if (argc > 1) {
		while (--argc > 0) {
			argv++;
			ct = lookup_cmd(cmd_table, *argv);
			if (ct)
				printf("%s\n", ct->ct_usage);
			else
				printf("Not a command: %s\n", *argv);
		}
		return(0);
	}
	printf("Commands:\n");
	for (ct = cmd_table; ct->ct_string; ct++) {
		if (*ct->ct_string == '.')
#ifndef DEBUG
			continue
#endif
			;
		printf("\t%s\n", ct->ct_usage);
	}
#ifdef DEBUG
	printf("\nCOMMAND FLAGS\n");
	printf("\tcommands that reference memory take widths of:\n");
	printf("\t\t-b -- byte, -h -- halfword, -w -- word (default)\n");
	printf("\tRANGE's are specified as one of:\n");
	printf("\t\tBASE_ADDRESS#COUNT\n");
	printf("\t\tSTART_ADDRESS:END_ADDRESS\n");
	printf("Erase single characters by CTRL-H or DEL\n");
	printf("Rubout entire line by CTRL-U\n");
#endif
	return(0);
}

int
parse_range(register char *cp, register int size, register struct range *ra)
{
	register __psint_t sizemask;

	ASSERT((size & (size - 1)) == 0);
	cp = atob_ptr(cp, &ra->ra_base);
	sizemask = ~(size - 1);
	ra->ra_base &= sizemask;
	switch (*cp) {
	  case ':':
#if _MIPS_SIM == _ABI64
		if (*atobu_L(++cp, (unsigned long long *)&ra->ra_count))
#else
                if (*atobu(++cp, (unsigned *)&ra->ra_count))
#endif
			return 0;
                ra->ra_count = ((ra->ra_count&sizemask) - ra->ra_base) / size;
                break;
	  case '#':
#if _MIPS_SIM == _ABI64
		if (*atobu_L(++cp, (unsigned long long *)&ra->ra_count))
                        return 0;
#else
		if (*atobu(++cp, (unsigned *)&ra->ra_count))
			return 0;
#endif
		break;
	  case '\0':
		ra->ra_count = 1;
		break;
	  default:
		return 0;
	}
	if (ra->ra_count == 0)
		ra->ra_count = 1;
	ra->ra_size = size;
	return 1;
}
