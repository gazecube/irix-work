#ifndef CLOGIN_PANDORA_RUNTIME_H
#define CLOGIN_PANDORA_RUNTIME_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int run_pandora_gui(int argc, char **argv, char *username_out, size_t username_len);

#ifdef __cplusplus
}
#endif

#endif /* CLOGIN_PANDORA_RUNTIME_H */
