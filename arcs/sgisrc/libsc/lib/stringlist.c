#ident	"lib/libsc/lib/stringlist.c:  $Revision: 1.13 $"

/*
 * stringlist.c -- string_list manipulation routines
 *	string_lists consist of a list of pointers into an byte array
 *	the list is null terminated
 *	(INVARIANT: strptrs[strcnt] == NULL)
 */

#include <stringlist.h>
#include <libsc.h>

static void garbage_collect(struct string_list *);
static void _delete_str(char *name, struct string_list *slp, char *oldname);

void
init_str(struct string_list *slp)
{
	slp->strp = slp->strbuf;
	slp->strcnt = 0;
	slp->strptrs[0] = 0;
}

void
replace_str(char *name, char *value, struct string_list *slp)
{
#define MAXRETNAME	80
	char buf[MAXRETNAME];

	buf[0] = '\0';
	_delete_str(name, slp, buf);
	new_str2(buf[0] ? buf : name, value, slp);
}

void
delete_str(char *name, struct string_list *slp)
{
	_delete_str(name,slp,0);
}

static void
_delete_str(char *name, struct string_list *slp, char *oldname)
{
	register char **np;
	register unsigned namelen;

	namelen = strlen(name);
	for (np = slp->strptrs;
	    np < &slp->strptrs[slp->strcnt]; np++)
		if (strncasecmp(name, *np, namelen) == 0 && (*np)[namelen] == '=') {
			if (oldname && (namelen < (MAXRETNAME-1))) {
				strncpy(oldname,*np,namelen);
				*(oldname+namelen) = '\0';
			}

			while (*np = *(np+1))
				np++;
			slp->strcnt--;
			return;
		}
	return;
}

char *
find_str(char *name, struct string_list *slp)
{
	register char **np;
	register unsigned namelen;

	namelen = strlen(name);
	for (np = slp->strptrs; np < &slp->strptrs[slp->strcnt]; np++) 
		if (strncasecmp(*np, name, namelen) == 0 && (*np)[namelen] == '=')
			return(*np);
	return(0);
}

int
new_str1(char *strp, struct string_list *slp)
{
	register int len;

	if (slp->strcnt >= MAXSTRINGS - 1) {
		printf("too many strings\n");
		return(-1);
	}

	len = strlen(strp) + 1;
	if (slp->strp + len >= &slp->strbuf[STRINGBYTES]) {
		garbage_collect(slp);
		if (slp->strp + len >= &slp->strbuf[STRINGBYTES]) {
			printf("no space left in string table\n");
			return(-1);
		}
	}

	slp->strptrs[slp->strcnt++] = slp->strp;
	slp->strptrs[slp->strcnt] = 0;
	strcpy(slp->strp, strp);
	slp->strp += len;
	return(0);
}

int
new_str2(char *name, char *value, struct string_list *slp)
{
	register int len;

#if DEBUG
	extern int Debug;

	if (Debug) {
		printf ("\tnew_str2(%s, %s) ", name, value);
	}
#endif
	if (slp->strcnt >= MAXSTRINGS - 1) {
		printf("too many strings\n");
		return(-1);
	}

	len = strlen(name) + strlen(value) + 2;
	if (slp->strp + len >= &slp->strbuf[STRINGBYTES]) {
		garbage_collect(slp);
		if (slp->strp + len >= &slp->strbuf[STRINGBYTES]) {
			printf("no space left in string table\n");
			return(-1);
		}
	}

	slp->strptrs[slp->strcnt++] = slp->strp;
	slp->strptrs[slp->strcnt] = 0;
	strcpy(slp->strp, name);
	strcat(slp->strp, "=");
	strcat(slp->strp, value);
	slp->strp += len;
	return(0);
}

int
set_str(char *strp, int strindex, struct string_list *slp)
{
	register int len;

	if (strindex >= slp->strcnt) {
		printf("no such string\n");
		return(-1);
	}

	len = strlen(strp) + 1;
	if (slp->strp + len >= &slp->strbuf[STRINGBYTES]) {
		garbage_collect(slp);
		if (slp->strp + len >= &slp->strbuf[STRINGBYTES]) {
			printf("no space left in string table\n");
			return(-1);
		}
	}

	slp->strptrs[strindex] = slp->strp;
	strcpy(slp->strp, strp);
	slp->strp += len;
	return(0);
}

int
delete_strnum(int num, struct string_list *slp)
{
	register char **np;

	if (num < 0 || num >= slp->strcnt)
		return(1);
	for (np = &slp->strptrs[num]; *np = *(np+1); np++)
		continue;
	return(0);
}

static void
garbage_collect(struct string_list *slp)
{
	struct string_list tmplist;
	register char **np;

	init_str(&tmplist);
	for (np = slp->strptrs; *np; np++)
		new_str1(*np, &tmplist);
	init_str(slp);
	for (np = tmplist.strptrs; *np; np++)
		new_str1(*np, slp);
}

char **
_copystrings(char **wp, struct string_list *slp)
{
	init_str(slp);
	for (; wp && *wp; wp++)
		new_str1(*wp, slp);
	return(slp->strptrs);
}
