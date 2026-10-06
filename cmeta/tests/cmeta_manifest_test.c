#include <cmeta/manifest.h>
#include "tinytest.h"
#include <string.h>

static const int json_codec = 1;
static const int xml_codec = 2;

cmeta_registry(codec_manifest,
    cmeta_entry(json_codec)
    cmeta_entry(xml_codec)
);

suite("CMeta static manifest") {
    it("builds immutable ordered registry tables") {
        check_equal(codec_manifest.format_version, CMETA_MANIFEST_FORMAT_VERSION);
        check_equal(codec_manifest.count, (size_t)2u);
        check_equal(codec_manifest.entries[0].descriptor, (const void *)&json_codec);
        check_equal(codec_manifest.entries[1].descriptor, (const void *)&xml_codec);
        check_equal(codec_manifest.entries[0].kind, CMETA_MANIFEST_GENERIC);
        check_equal(strcmp(codec_manifest.entries[0].name, "json_codec"), 0);
        check_equal(strcmp(codec_manifest.entries[1].name, "xml_codec"), 0);
    }

    it("fingerprints canonical type metadata without using descriptor addresses") {
        cmeta_type_identity atom_a = CMETA_TYPE_ID_ATOM_INIT("example.scalar");
        cmeta_type_identity atom_b = CMETA_TYPE_ID_ATOM_INIT("example.scalar");
        cmeta_type_desc a = {
            "Example", 8u, 8u, CMETA_T_INTEGER, NULL, &cmeta_traits_int, &atom_a
        };
        cmeta_type_desc b = {
            "Example", 8u, 8u, CMETA_T_INTEGER, NULL, &cmeta_traits_int, &atom_b
        };
        cmeta_type_desc changed = {
            "Example", 16u, 8u, CMETA_T_INTEGER, NULL, &cmeta_traits_int, &atom_b
        };

        check_equal(cmeta_abi_fingerprint_type(&a), cmeta_abi_fingerprint_type(&b));
        check_not_equal(cmeta_abi_fingerprint_type(&a),
                        cmeta_abi_fingerprint_type(&changed));
    }

    it("uses explicit fingerprint algorithm versioning") {
        cmeta_abi_fingerprint_builder a = cmeta_abi_fingerprint_begin();
        cmeta_abi_fingerprint_builder b = cmeta_abi_fingerprint_begin();

        cmeta_abi_fingerprint_u64(&a, CMETA_ABI_FINGERPRINT_VERSION);
        cmeta_abi_fingerprint_string(&a, "stable");
        cmeta_abi_fingerprint_u64(&b, CMETA_ABI_FINGERPRINT_VERSION);
        cmeta_abi_fingerprint_string(&b, "stable");

        check_equal(cmeta_abi_fingerprint_finish(&a),
                    cmeta_abi_fingerprint_finish(&b));
    }
}
