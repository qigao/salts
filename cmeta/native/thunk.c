#include "code_memory.h"
#include <string.h>

enum { CMETA_NATIVE_CODE_BYTES = 28, CMETA_NATIVE_ADDRESS_BYTES = 8 };
_Static_assert(sizeof(int) == 4 && sizeof(void *) == CMETA_NATIVE_ADDRESS_BYTES,
               "Native i32 thunks require the qualified x86-64 ABI");
_Static_assert(sizeof(cmeta_native_i32_fn) == CMETA_NATIVE_ADDRESS_BYTES &&
                   sizeof(cmeta_native_context_i32_fn) == CMETA_NATIVE_ADDRESS_BYTES,
               "Native thunk function address width mismatch");

static bool native_int(const cmeta_type_desc *type) {
  return cmeta_type_equal(type, &cmeta_type_int) && type->kind == CMETA_T_INTEGER &&
         type->size == sizeof(int) && type->align == _Alignof(int);
}
static bool native_i32_abi(const cmeta_function_abi_desc *abi) {
  return cmeta_function_abi_desc_valid(abi) && abi->param_count == 1 &&
         abi->return_carrier == CMETA_ABI_SCALAR && abi->param_carriers[0] == CMETA_ABI_SCALAR &&
         native_int(abi->function->return_type) && native_int(abi->function->params[0].type) &&
         abi->function->params[0].flags == CMETA_PARAM_IN &&
         abi->function->result_flags == CMETA_RESULT_VALUE;
}
cmeta_status cmeta_native_i32_admit(const cmeta_function_abi_desc *abi, cmeta_native_i32_fn target,
                                    cmeta_native_binding *out) {
  const cmeta_native_binding empty = CMETA_NATIVE_BINDING_INIT;
  if (out == NULL) return CMETA_INVALID_ARGUMENT;
  *out = empty;
  if (abi == NULL || target == NULL) return CMETA_INVALID_ARGUMENT;
  if (!native_i32_abi(abi)) return CMETA_TYPE_MISMATCH;
  out->abi = abi;
  out->kind = CMETA_NATIVE_I32;
  out->direct = target;
  return CMETA_OK;
}
cmeta_status cmeta_native_context_i32_admit(const cmeta_function_abi_desc *source,
                                            const cmeta_function_abi_desc *projected,
                                            cmeta_native_context_i32_fn target, void *context,
                                            cmeta_native_binding *out) {
  const cmeta_native_binding empty = CMETA_NATIVE_BINDING_INIT;
  const cmeta_param_desc *receiver;
  if (out == NULL) return CMETA_INVALID_ARGUMENT;
  *out = empty;
  if (source == NULL || projected == NULL || target == NULL) return CMETA_INVALID_ARGUMENT;
  if (!native_i32_abi(projected) || !cmeta_function_abi_desc_valid(source) ||
      source->param_count != 2 || source->return_carrier != CMETA_ABI_SCALAR ||
      !native_int(source->function->return_type) || !native_int(source->function->params[1].type) ||
      source->param_carriers[0] != CMETA_ABI_OBJECT_POINTER ||
      source->param_carriers[1] != CMETA_ABI_SCALAR ||
      !cmeta_function_receiver_projection_valid(source->function, projected->function))
    return CMETA_TYPE_MISMATCH;
  receiver = &source->function->params[0];
  if (receiver->type->kind != CMETA_T_POINTER || receiver->type->size != sizeof(void *) ||
      receiver->type->align != _Alignof(void *) || (receiver->flags & CMETA_PARAM_BORROWED) == 0 ||
      (receiver->flags & CMETA_PARAM_OWNED) != 0)
    return CMETA_TYPE_MISMATCH;
  if (context == NULL && (receiver->flags & CMETA_PARAM_NULLABLE) == 0)
    return CMETA_INVALID_ARGUMENT;
  out->abi = projected;
  out->kind = CMETA_NATIVE_CONTEXT_I32;
  out->contextual = target;
  out->context = context;
  return CMETA_OK;
}

static bool native_binding_ready(const cmeta_native_binding *binding) {
  return binding != NULL && binding->abi != NULL &&
         ((binding->kind == CMETA_NATIVE_I32 && binding->direct != NULL) ||
          (binding->kind == CMETA_NATIVE_CONTEXT_I32 && binding->contextual != NULL));
}

/* Fixed exact shape, O(1) time/space. Only volatile registers change; the
 * original caller's return address and (Win64) shadow store remain intact.
 * ENDBR64 admits indirect entry on CET-enabled x86-64 hosts. */
static void native_emit(const cmeta_native_binding *binding, unsigned char *code) {
  static const unsigned char entry[] = {0xf3, 0x0f, 0x1e, 0xfa};
  static const unsigned char target_load[] = {0x48, 0xb8};
  static const unsigned char target_jump[] = {0xff, 0xe0};
#ifdef _WIN32
  static const unsigned char bind_context[] = {0x89, 0xca, 0x48, 0xb9};
#else
  static const unsigned char bind_context[] = {0x89, 0xfe, 0x48, 0xbf};
#endif
  size_t offset = sizeof(entry);
  memcpy(code, entry, sizeof(entry));
  if (binding->kind == CMETA_NATIVE_CONTEXT_I32) {
    memcpy(code + offset, bind_context, sizeof(bind_context));
    offset += sizeof(bind_context);
    memcpy(code + offset, &binding->context, CMETA_NATIVE_ADDRESS_BYTES);
    offset += CMETA_NATIVE_ADDRESS_BYTES;
  }
  memcpy(code + offset, target_load, sizeof(target_load));
  offset += sizeof(target_load);
  if (binding->kind == CMETA_NATIVE_I32)
    memcpy(code + offset, &binding->direct, CMETA_NATIVE_ADDRESS_BYTES);
  else memcpy(code + offset, &binding->contextual, CMETA_NATIVE_ADDRESS_BYTES);
  offset += CMETA_NATIVE_ADDRESS_BYTES;
  memcpy(code + offset, target_jump, sizeof(target_jump));
}

static cmeta_status native_publish(cmeta_native_thunk *thunk, const cmeta_native_binding *binding) {
  unsigned char code[CMETA_NATIVE_CODE_BYTES] = {0};
  cmeta_status status;
  native_emit(binding, code);
  memcpy(thunk->allocation, code, sizeof(code));
  status = cmeta_native_memory_publish(thunk);
  if (status == CMETA_OK) thunk->abi = binding->abi;
  return status;
}
cmeta_status cmeta_native_thunk_create(const cmeta_native_binding *binding, size_t max_code_bytes,
                                       cmeta_native_thunk *out) {
  cmeta_status status;
  if (out == NULL || !native_binding_ready(binding)) return CMETA_INVALID_ARGUMENT;
  if (out->allocation != NULL || out->state != CMETA_NATIVE_EMPTY) return CMETA_BUSY;
  if (max_code_bytes < CMETA_NATIVE_CODE_BYTES) return CMETA_CAPACITY_EXCEEDED;
  status = cmeta_native_memory_create(max_code_bytes, out);
  if (status != CMETA_OK) return status;
  return native_publish(out, binding);
}
cmeta_status cmeta_native_thunk_rebind(cmeta_native_thunk *thunk,
                                       const cmeta_native_binding *binding) {
  cmeta_status status;
  if (thunk == NULL || thunk->allocation == NULL || !native_binding_ready(binding))
    return CMETA_INVALID_ARGUMENT;
  if (thunk->abi != NULL && !cmeta_function_abi_contract_compatible(thunk->abi, binding->abi))
    return CMETA_TYPE_MISMATCH;
  status = cmeta_native_memory_write(thunk);
  return status == CMETA_OK ? native_publish(thunk, binding) : status;
}
cmeta_native_i32_fn cmeta_native_thunk_entry(const cmeta_native_thunk *thunk) {
  cmeta_native_i32_fn entry = NULL;
  if (thunk != NULL && thunk->state == CMETA_NATIVE_READY)
    memcpy(&entry, &thunk->allocation, sizeof(entry));
  return entry;
}
cmeta_status cmeta_native_thunk_destroy(cmeta_native_thunk *thunk) {
  const cmeta_native_thunk empty = CMETA_NATIVE_THUNK_INIT;
  cmeta_status status;
  if (thunk == NULL) return CMETA_INVALID_ARGUMENT;
  if (thunk->allocation == NULL) return CMETA_OK;
  status = cmeta_native_memory_destroy(thunk);
  if (status == CMETA_OK) *thunk = empty;
  return status;
}
