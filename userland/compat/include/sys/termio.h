/* IRIX <termio.h>: the System V terminal interface (struct termio, TCGETA, TCSETA, TCSETAW, TCSETAF) maps onto the
 * host's, which keeps it in <sys/ioctl.h> with Linux's ioctl numbers; IRIX's own header has IRIX's numbers */
#include <termios.h>
#include <sys/ioctl.h>
