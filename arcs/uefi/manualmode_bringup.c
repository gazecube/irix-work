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

/* The complete IP22 command table is being brought across command by command
 * from the original PROM sources.  Keep the first live table deliberately
 * source-backed: help is implemented by pristine parser.c itself. */
static struct cmd_table ArcsPromCmdTable[] = {
    { "help", help, "help:\t\thelp or ? [COMMAND]" },
    { ".?",   help, "help:\t\t? [COMMAND]" },
    { 0,       0,    "" }
};

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
     * returns from menu_manualmode(). */
    command_parser(ArcsPromCmdTable, ">> ", 1, 0);
}
