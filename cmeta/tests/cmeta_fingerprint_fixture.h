#ifndef CMETA_FINGERPRINT_FIXTURE_H
#define CMETA_FINGERPRINT_FIXTURE_H
#include <cmeta/fingerprint.h>
#include <cmeta/manifest_view.h>
#include <cmeta/flags.h>

/* A fixed-width native row keeps the published golden vectors independent of
 * int/long spelling. Each translation unit/image owns its canonical metadata. */
static const cmeta_type_identity fingerprint_word_id = CMETA_TYPE_ID_ATOM_INIT("test.word32");
static const cmeta_type_desc fingerprint_word = {
    "Word", sizeof(uint32_t), CMETA_ALIGNOF(uint32_t), CMETA_T_INTEGER,
    NULL, NULL, &fingerprint_word_id
};
typedef struct fingerprint_record { uint32_t value; } fingerprint_record;
static const cmeta_field_desc fingerprint_fields[] = {
    {"value", "uint32_t", offsetof(fingerprint_record, value), sizeof(uint32_t),
        CMETA_ALIGNOF(uint32_t), &fingerprint_word, NULL}
};
static const cmeta_struct_desc fingerprint_record_meta = {
    "Record", sizeof(fingerprint_record), CMETA_ALIGNOF(fingerprint_record),
    fingerprint_fields, sizeof(fingerprint_fields) / sizeof(fingerprint_fields[0])
};
CMETA_FUNCTION_METADATA_AS_ABI(fingerprint_transform, "fingerprint_transform",
    value, &fingerprint_word, CMETA_ABI_SCALAR,
    (uint32_t, value, CMETA_PARAM_IN, &fingerprint_word, CMETA_ABI_SCALAR));
#define FINGERPRINT_SERVICE_METHODS(X,I) \
    X(I,F0,uint32_t,read,value,&fingerprint_word,CMETA_ABI_SCALAR)
CMETA_INTERFACE(FingerprintService, FINGERPRINT_SERVICE_METHODS);
cmeta_flags(FingerprintFlags, "test.flags",
    cmeta_flag(FingerprintLow, UINT64_C(1), "low")
    cmeta_flag(FingerprintHigh, UINT64_C(0x8000000000000000), "high"));
cmeta_registry(fingerprint_manifest,
    cmeta_manifest_enum_entry("flags", &FingerprintFlags__domain));

static const cmeta_fingerprint_limits fingerprint_limits = {
    CMETA_FINGERPRINT_DEFAULT_DEPTH, CMETA_FINGERPRINT_DEFAULT_NODES,
    CMETA_FINGERPRINT_DEFAULT_ROWS, CMETA_FINGERPRINT_DEFAULT_STRING_BYTES
};
enum { FP_TYPE, FP_STRUCT, FP_ENUM, FP_FUNCTION, FP_INTERFACE, FP_COUNT };
/* Vectors are independently calculated from the documented v1 byte grammar. */
static const uint64_t fingerprint_golden[FP_COUNT] = {
    UINT64_C(0xda23bb506d89ad46),
    UINT64_C(0x61362ebbad0aa96e),
    UINT64_C(0xfdcfb9ac20892503),
    UINT64_C(0x6b5430608eacabce),
    UINT64_C(0xc34c77442ac68534)
};

typedef struct cmeta_fingerprint_fixture {
    const cmeta_type_desc *type;
    const cmeta_struct_desc *structure;
    const cmeta_enum_domain *enumeration;
    const cmeta_function_abi_desc *function;
    const cmeta_interface_desc *interface_desc;
} cmeta_fingerprint_fixture;
static const cmeta_fingerprint_fixture fingerprint_fixture = {
    &fingerprint_word, &fingerprint_record_meta, &FingerprintFlags__domain,
    (&fingerprint_transform__function_abi_meta), &FingerprintService_interface_meta
};

#ifdef __cplusplus
extern "C" {
#endif
const cmeta_fingerprint_fixture *cmeta_fingerprint_peer(void);
cmeta_status cmeta_fingerprint_peer_values(uint64_t out[FP_COUNT]);
#ifdef CMETA_FINGERPRINT_DSO_TEST
#if defined(_WIN32)
#if defined(CMETA_FINGERPRINT_DSO_BUILD)
#define CMETA_FINGERPRINT_DSO_API __declspec(dllexport)
#else
#define CMETA_FINGERPRINT_DSO_API __declspec(dllimport)
#endif
#else
#define CMETA_FINGERPRINT_DSO_API __attribute__((visibility("default")))
#endif
CMETA_FINGERPRINT_DSO_API const cmeta_fingerprint_fixture *
cmeta_fingerprint_dso_query(uint32_t reflection_epoch);
CMETA_FINGERPRINT_DSO_API cmeta_status
cmeta_fingerprint_dso_values(uint32_t reflection_epoch, uint64_t out[FP_COUNT]);
#endif
#ifdef __cplusplus
}
#endif
#endif
