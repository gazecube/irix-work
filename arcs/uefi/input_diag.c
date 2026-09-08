#include <Uefi.h>

#include "input_diag.h"

/*
 * The UEFI text-mode pointer overlay was useful during input bring-up, but
 * pointer motion, hover, and button delivery are now proven through the
 * original SGI GUI event path. Keep the diagnostic entry point as a no-op for
 * now so the input instrumentation can remain available without painting over
 * the PROM UI.
 */
VOID
ArcsInputDiagDraw(EFI_SYSTEM_TABLE *SystemTable, const ARCS_INPUT_DIAG *Diag)
{
    (VOID)SystemTable;
    (VOID)Diag;
}
