/*
 * X64/UEFI platform hooks required by pristine SGI host textport code.
 *
 * These are machine-dependent services in the original PROM.  Keep their
 * original external contracts while the UEFI implementations live at the
 * architecture/platform boundary rather than in pristine SGI sources.
 */

void
_mspoll(void)
{
    /* Original htport calls _mspoll() while repainting dirty text rows so an
     * SGI mouse driver can keep pointer tracking current.  The UEFI pointer
     * path is event-driven outside the textport and Command Monitor does not
     * require an SGI mouse device, so there is no device poll to perform here.
     * A future canonical UEFI mouse bridge can be called from this hook. */
}

void
bell(void)
{
    /* The bell is platform hardware on the original machines.  Keep the
     * contract without inventing a UEFI audio device.  This mirrors SGI
     * platforms whose standalone bell implementation is intentionally empty. */
}
