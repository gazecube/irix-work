/*
 * Product selection boundary for the X64/UEFI PROM port.
 *
 * Original stand/arcs/include/product.h maps IP22 to logo/indigo2.h.  Keep
 * the pristine bitmap data and symbols, but select that product explicitly
 * here instead of defining IP22 globally and accidentally enabling unrelated
 * MIPS/IP22 machine-dependent source paths.
 */
#include <logo/indigo2.h>
