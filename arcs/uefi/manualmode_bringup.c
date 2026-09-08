/*
 * UEFI outer-loop bridge for the original SGI Command Monitor path.
 *
 * The body of menu_manualmode() is source-derived from
 * stand/arcs/lib/libsk/cmd/pmenu_cmd.c.  The original PROM returns from
 * menu_manualmode() to its main loop and then calls command_parser().  Until
 * that complete machine-dependent main loop is linked, this bridge performs
 * that one outer-loop step here without changing the pristine parser.
 */

#include <arcs/io.h>
#include <gfxgui.h>
#include <libsc.h>
#include <parser.h>
#include <setjmp.h>

extern void exit_cmd(int, char **);
extern int printenv_cmd(int, char **, char **, struct cmd_table *);
extern int setenv_cmd(int, char **, char **, struct cmd_table *);
extern int unsetenv_cmd(int, char **, char **, struct cmd_table *);

/*
 * Entries and help strings below are copied from the IP22 PROM cmd_table in
 * stand/arcs/IP22prom/main.c.  Expand this table only as each original command
 * implementation and its platform dependencies are linked.
 */
static struct cmd_table ArcsPromCmdTable[] = {
    { "exit",     (CT_ROUTINE exit_cmd), "exit:\t\texit" },
    { "help",     help,                  "help:\t\thelp or ? [COMMAND]" },
    { ".?",       help,                  "help:\t\t? [COMMAND]" },
    { "printenv", printenv_cmd,          "printenv:\tprintenv [ENV_VAR_LIST]" },
    { "setenv",   setenv_cmd,            "setenv:\t\tsetenv ENV_VAR STRING" },
    { "unsetenv", unsetenv_cmd,          "unsetenv:\tunsetenv ENV_VAR" },
    { 0,           0,                     "" }
};

static jmp_buf ArcsMonitorReturn;
static int ArcsMonitorReturnActive;

/*
 * Original libsc eimode.c calls the firmware transfer vector's
 * EnterInteractiveMode entry.  On the native UEFI port the surrounding PROM
 * main loop is this bridge, so reproduce that transfer by returning to the
 * point immediately outside command_parser().
 */
void
EnterInteractiveMode(void)
{
    if (ArcsMonitorReturnActive)
        longjmp(ArcsMonitorReturn, 1);
}

int
menu_manualmode(void)
{
    /* Original pmenu_cmd.c installs the graphical textport Done button here
     * through setTpButtonAction(EnterInteractiveMode,...).  tpbutton.c writes
     * a MIPS JB_PC slot directly, so that one architecture-specific return
     * hook remains deferred until its X64 transfer-vector equivalent exists.
     * The actual text-mode transition and message remain original. */
    p_textmode();
    p_clear();

    printf("Command Monitor.  Type \"exit\" %sto return to the menu.\n",
           isgraphic(StandardOut) ? "or click on \"done\" " : "");

    return 1;
}

void
ArcsEnterCommandMonitor(void)
{
    if (!menu_manualmode())
        return;

    /* Original IP22 main.c performs this immediately after menu_parser()
     * returns from menu_manualmode().  EnterInteractiveMode() transfers back
     * here on native X64 rather than jumping through a MIPS firmware vector. */
    if (setjmp(ArcsMonitorReturn) != 0) {
        ArcsMonitorReturnActive = 0;
        return;
    }

    ArcsMonitorReturnActive = 1;
    command_parser(ArcsPromCmdTable, ">> ", 1, 0);
    ArcsMonitorReturnActive = 0;
}
