#ifndef _IRIX_SYS_VAR_H
#define _IRIX_SYS_VAR_H
/* Only the field init reads: the size of the process table. */
struct var {
	int v_proc;
};
#endif
