/*
 * Real signal delivery for guest programs.
 *
 * The bridge used to only record sigaction() in a table; no host handler was
 * ever installed, so guest handlers never ran (xdm waits for the X server's
 * SIGUSR1 in sigsuspend and could never wake).  Here:
 *   - IRIX and Linux signal numbers are mapped (they differ: IRIX SIGUSR1 is
 *     16, SIGCLD 18, ...);
 *   - a host handler records the signal;
 *   - the guest handler runs at the next bridge-call boundary (the dispatcher
 *     calls irix_guest_signal_dispatch) or when a blocking call returns.
 * The guest's own signal mask stays virtual (sigprocmask bridge): a pending
 * signal that the guest has masked is held until it is unmasked.
 */
#include "IrixGuest.h"

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#define IRIX_NSIG_MAX 32
#define IRIX_SIG_DFL 0u
#define IRIX_SIG_IGN 1u

/* index: IRIX signal number -> Linux number (0 = none) */
static const int g_i2h[IRIX_NSIG_MAX + 1] = {
    0,
    SIGHUP, SIGINT, SIGQUIT, SIGILL, SIGTRAP, SIGABRT, 0 /*EMT*/, SIGFPE,
    SIGKILL, SIGBUS, SIGSEGV, SIGSYS, SIGPIPE, SIGALRM, SIGTERM,
    SIGUSR1, SIGUSR2, SIGCHLD, SIGPWR, SIGWINCH, SIGURG, SIGIO,
    SIGSTOP, SIGTSTP, SIGCONT, SIGTTIN, SIGTTOU, SIGVTALRM, SIGPROF,
    SIGXCPU, SIGXFSZ,
};

int irix_signal_to_host(int irix_sig)
{
    if (irix_sig <= 0 || irix_sig > IRIX_NSIG_MAX) return irix_sig;
    return g_i2h[irix_sig];
}

static int host_to_irix(int host_sig)
{
    int i;
    for (i = 1; i <= IRIX_NSIG_MAX; ++i)
        if (g_i2h[i] == host_sig) return i;
    return 0;
}

static uint32_t g_handler[IRIX_NSIG_MAX + 1];
static volatile sig_atomic_t g_pending[IRIX_NSIG_MAX + 1];
static sigset_t g_installed;
static int g_installed_init;

extern volatile int irix_guest_signals_pending;
extern void (*irix_guest_signal_dispatch)(IrixGuestRuntime *, IrixGuestCpu *);
/* guest signal mask (IRIX sigset words, bit sig-1), maintained elsewhere */
extern uint32_t g_irix_sigmask[4];

static void host_handler(int host_sig)
{
    int s = host_to_irix(host_sig);
    if (s > 0) {
        g_pending[s] = 1;
        irix_guest_signals_pending = 1;
    }
}

static void ensure_init(void)
{
    if (!g_installed_init) {
        sigemptyset(&g_installed);
        g_installed_init = 1;
    }
}

/* sigaction(2)/signal(3)/sigset(3) disposition from the guest */
int irix_signal_install(int irix_sig, uint32_t handler)
{
    int hs = irix_signal_to_host(irix_sig);
    struct sigaction sa;
    if (irix_sig <= 0 || irix_sig > IRIX_NSIG_MAX || hs <= 0)
        return -1;
    if (hs == SIGKILL || hs == SIGSTOP)
        return -1;
    ensure_init();
    g_handler[irix_sig] = handler;
    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);
    if (handler == IRIX_SIG_DFL) {
        sa.sa_handler = SIG_DFL;
        sigdelset(&g_installed, hs);
    } else if (handler == IRIX_SIG_IGN) {
        sa.sa_handler = SIG_IGN;
        sigdelset(&g_installed, hs);
    } else {
        sa.sa_handler = host_handler;   /* no SA_RESTART: blocking calls EINTR */
        sigaddset(&g_installed, hs);
    }
    return sigaction(hs, &sa, NULL);
}

static int guest_blocked(int s)
{
    return (g_irix_sigmask[(s - 1) >> 5] >> ((s - 1) & 31)) & 1u;
}

int irix_signal_any_pending(void)
{
    int s;
    for (s = 1; s <= IRIX_NSIG_MAX; ++s)
        if (g_pending[s] && !guest_blocked(s)) return 1;
    return 0;
}

static void dispatch(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    static int busy;
    int s, again;
    if (busy) return;
    busy = 1;
    do {
        again = 0;
        irix_guest_signals_pending = 0;
        for (s = 1; s <= IRIX_NSIG_MAX; ++s) {
            uint32_t h;
            if (!g_pending[s] || guest_blocked(s)) continue;
            g_pending[s] = 0;
            h = g_handler[s];
            if (h > 2u && !rt->trapped) {
                IrixGuestCpu c = *cpu;
                c.gpr[4] = (uint64_t)s;
                c.gpr[25] = (uint64_t)h;    /* PIC: $gp is derived from t9 */
                c.gpr[5] = 0;
                c.gpr[6] = 0;
                c.gpr[29] -= 128u;      /* stay below the interrupted frame */
                (void)irix_guest_call(rt, &c, (IrixGuestAddr)h);
                again = 1;
            }
        }
        if (irix_signal_any_pending()) again = 1;
    } while (again && !rt->trapped);
    /* keep the flag set for masked signals so they are retried later */
    for (s = 1; s <= IRIX_NSIG_MAX; ++s)
        if (g_pending[s]) { irix_guest_signals_pending = 1; break; }
    busy = 0;
}

void irix_signal_bridge_init(void)
{
    irix_guest_signal_dispatch = dispatch;
}

/* sigsuspend(mask): atomically replace the mask and wait for a signal */
int irix_signal_suspend(void)
{
    sigset_t block, old;
    ensure_init();
    sigemptyset(&block);
    block = g_installed;
    sigprocmask(SIG_BLOCK, &block, &old);
    if (!irix_signal_any_pending()) {
        sigset_t wait = old;
        int i;
        for (i = 1; i < 65; ++i)
            if (sigismember(&g_installed, i) == 1) sigdelset(&wait, i);
        (void)sigsuspend(&wait);
    }
    sigprocmask(SIG_SETMASK, &old, NULL);
    errno = EINTR;
    return -1;
}
