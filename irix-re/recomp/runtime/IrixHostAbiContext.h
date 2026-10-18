#ifndef IRIX_HOST_ABI_CONTEXT_H
#define IRIX_HOST_ABI_CONTEXT_H

#include "IrixGuest.h"
#include "IrixN32Abi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct IrixHostAbiContext IrixHostAbiContext;

/* Create one isolated translated Xt/Motif state universe for a host process. */
IrixHostAbiContext *irix_host_abi_context_create(void);
void irix_host_abi_context_destroy(IrixHostAbiContext *context);

/* Invoke a registered translated N32 entry using 32-bit integer/pointer args. */
int irix_host_abi_context_call(IrixHostAbiContext *context, const char *name,
                               const uint32_t *args, size_t arg_count,
                               uint32_t *result);

typedef enum IrixHostAbiResultKind {
    IRIX_HOST_ABI_RESULT_VOID,
    IRIX_HOST_ABI_RESULT_I32,
    IRIX_HOST_ABI_RESULT_U32,
    IRIX_HOST_ABI_RESULT_PTR32,
    IRIX_HOST_ABI_RESULT_I64,
    IRIX_HOST_ABI_RESULT_U64,
    IRIX_HOST_ABI_RESULT_F32,
    IRIX_HOST_ABI_RESULT_F64
} IrixHostAbiResultKind;

/* Signature-aware N32 scalar call for newly generated/native wrappers. */
int irix_host_abi_context_call_typed(IrixHostAbiContext *context,
                                     const char *name,
                                     const IrixN32AbiArgument *args,
                                     size_t arg_count,
                                     IrixHostAbiResultKind result_kind,
                                     uint64_t *result_bits);

/* Copy a host byte string into context-owned N32 memory for wrapper inputs. */
int irix_host_abi_context_copy_string(IrixHostAbiContext *context,
                                      const char *value,
                                      IrixGuestAddr *guest_address);
int irix_host_abi_context_copy_bytes(IrixHostAbiContext *context,
                                     const void *bytes, size_t size,
                                     IrixGuestAddr *guest_address);
int irix_host_abi_context_write32be(IrixHostAbiContext *context,
                                    IrixGuestAddr address, uint32_t value);
int irix_host_abi_context_read32be(IrixHostAbiContext *context,
                                   IrixGuestAddr address, uint32_t *value);
int irix_host_abi_context_has_function(IrixHostAbiContext *context,
                                       const char *name);

#ifdef __cplusplus
}
#endif

#endif
