#include "cmeta_data_reflect_fixture.h"
#include <cmeta/object.h>
#include <cmeta/fingerprint.h>

suite("CMeta data reflection of existing native records") {
    it("keeps partial views outside semantic lifecycle without changing objects") {
        NativeRecordView source = {7, {3, 4.5}, 9, true, 1.0f};
        NativeRecordView destination = source;
        const cmeta_data_desc *data = cmeta_reflected_data(NativeRecordView);
        cmeta_object_ref object;
        const cmeta_data_desc *field_data = NULL;
        const void *field = NULL;
        bool zero = true;
        cmeta_data_temp temp = {0};
        check_true(cmeta_data_desc_valid(data));
        check_false(cmeta_data_struct_constructible(data));
        check_false(cmeta_data_value_copy_supported(data));
        check_false(cmeta_data_value_move_supported(data));
        check_equal(cmeta_data_temp_open(data, sizeof(source), &temp), CMETA_TRAIT_MISSING);
        check_null(temp.storage);
        check_equal(cmeta_data_value_init_zero(data, &destination), CMETA_TRAIT_MISSING);
        check_equal(cmeta_data_value_restore_zero(data, &destination), CMETA_TRAIT_MISSING);
        check_equal(cmeta_data_value_copy(data, &destination, &source), CMETA_TRAIT_MISSING);
        check_equal(cmeta_data_value_move(data, &destination, &source), CMETA_TRAIT_MISSING);
        check_equal(cmeta_data_value_is_zero(data, &source, &zero), CMETA_TRAIT_MISSING);
        check_false(zero);
        check_equal(destination.serial, source.serial);
        check_equal(destination.renamed, source.renamed);
        check_equal(cmeta_object_borrow(&object, &source, data, NULL), CMETA_OK);
        check_equal(cmeta_object_field_read(&object, "serial", &field_data, &field), CMETA_OK);
        check_equal(*(const long *)field, source.serial);
        cmeta_object_release(&object);
    }
    it("reads const native fields without granting value lifecycle") {
        NativeConstView source = {7};
        cmeta_object_ref object;
        const cmeta_data_desc *field_data = NULL;
        const void *field = NULL;
        const cmeta_data_desc *data = cmeta_reflected_data(NativeConstView);
        check_false(cmeta_data_value_traits_supported(data));
        check_equal(cmeta_object_borrow(&object, &source, data, NULL), CMETA_OK);
        check_equal(cmeta_object_field_read(&object, "value", &field_data, &field), CMETA_OK);
        check_equal(*(const int *)field, source.value);
        cmeta_object_release(&object);
    }
    it("retains canonical nested storage for contract fingerprints across translation units") {
        const cmeta_fingerprint_limits limits = {CMETA_FINGERPRINT_DEFAULT_DEPTH,
            CMETA_FINGERPRINT_DEFAULT_NODES, CMETA_FINGERPRINT_DEFAULT_ROWS,
            CMETA_FINGERPRINT_DEFAULT_STRING_BYTES};
        const cmeta_data_struct_shape *peer =
            (const cmeta_data_struct_shape *)cmeta_test_peer_record_data()->shape;
        uint64_t local_fingerprint = 0, peer_fingerprint = 0;
        check_equal(cmeta_contract_fingerprint_struct(StructMeta(NativeRecord),
            &limits, &local_fingerprint), CMETA_OK);
        check_equal(cmeta_contract_fingerprint_struct(peer->layout,
            &limits, &peer_fingerprint), CMETA_OK);
        check_equal(local_fingerprint, peer_fingerprint);
        check_true(cmeta_type_equal(FieldFind(NativeRecord, "leaf")->type,
            cmeta_reflected_storage(NativeLeaf)));
    }
    it("rejects duplicate stable IDs and inconsistent explicit storage at admission") {
        cmeta_data_desc data = *cmeta_reflected_data(NativeLeaf);
        cmeta_data_reflection_shape shape = NativeLeaf__data_shape;
        cmeta_data_field_desc fields[] = {shape.structure.fields[0], shape.structure.fields[1]};
        cmeta_struct_desc layout = *shape.structure.layout;
        cmeta_field_desc layout_fields[] = {layout.fields[0], layout.fields[1]};
        NativeLeaf source = {3, 4.5};
        cmeta_object_ref object;
        data.shape = &shape;
        shape.structure.fields = fields;
        shape.structure.layout = &layout;
        layout.fields = layout_fields;
        fields[1].stable_id = fields[0].stable_id;
        check_false(cmeta_data_desc_valid(&data));
        check_equal(cmeta_object_borrow(&object, &source, &data, NULL), CMETA_INVALID_ARGUMENT);
        fields[1].stable_id = NativeLeaf__data_fields[1].stable_id;
        fields[0].value = &cmeta_data_float;
        check_false(cmeta_data_desc_valid(&data));
        fields[0].value = &cmeta_data_double;
        check_false(cmeta_data_desc_valid(&data));
        fields[0] = NativeLeaf__data_fields[0];
        layout_fields[0].type = NULL;
        check_false(cmeta_data_desc_valid(&data));
        layout_fields[0] = NativeLeaf__struct_fields[0];
        layout_fields[0].align = layout.align + layout.align;
        check_false(cmeta_data_desc_valid(&data));
        layout_fields[0] = NativeLeaf__struct_fields[0];
        check_true(cmeta_data_desc_valid(&data));
    }
    it("preserves v1 aggregate lifecycle while rejecting unknown descriptor versions") {
        cmeta_data_desc legacy = *cmeta_reflected_data(NativeLeaf);
        const cmeta_data_struct_shape legacy_shape = NativeLeaf__data_shape.structure;
        NativeLeaf source;
        legacy.abi_version = CMETA_DATA_DESC_ABI_VERSION;
        legacy.shape = &legacy_shape;
        check_true(cmeta_data_struct_constructible(&legacy));
        check_equal(cmeta_data_value_init_zero(&legacy, &source), CMETA_OK);
        check_equal(source.count, 0);
        legacy.abi_version = CMETA_DATA_DESC_REFLECTION_ABI_VERSION + 1u;
        check_false(cmeta_data_desc_valid(&legacy));
    }
    it("does not promote a nested projection to an owning value") {
        NativeViewParent source = {{7, {3, 4.5}, 9, true, 1.0f}};
        const cmeta_data_desc *data = cmeta_reflected_data(NativeViewParent);
        check_true(cmeta_data_desc_valid(data));
        check_false(cmeta_data_struct_constructible(data));
        check_false(cmeta_data_value_copy_supported(data));
        check_false(cmeta_data_value_move_supported(data));
        check_equal(cmeta_data_value_init_zero(data, &source), CMETA_TRAIT_MISSING);
        check_equal(source.child.serial, 7L);
    }
    it("rejects invalid reflection modes, extents, and overlapping value fields") {
        cmeta_data_desc data = *cmeta_reflected_data(NativeLeaf);
        cmeta_data_reflection_shape shape = NativeLeaf__data_shape;
        cmeta_struct_desc layout = *shape.structure.layout;
        cmeta_data_field_desc fields[] = {shape.structure.fields[0], shape.structure.fields[1]};
        cmeta_field_desc layout_fields[] = {layout.fields[0], layout.fields[1]};
        data.shape = &shape;
        shape.structure.layout = &layout;
        shape.structure.fields = fields;
        layout.fields = layout_fields;
        shape.struct_size = offsetof(cmeta_data_reflection_shape, mode);
        check_false(cmeta_data_desc_valid(&data));
        shape.struct_size = sizeof(shape);
        shape.mode = (cmeta_data_reflection_mode)(CMETA_DATA_REFLECTION_VALUE + 1);
        check_false(cmeta_data_desc_valid(&data));
        shape.mode = CMETA_DATA_REFLECTION_VALUE;
        shape.structure.field_count = CMETA_DATA_REFLECTION_MAX_FIELDS + 1u;
        check_false(cmeta_data_desc_valid(&data));
        shape.structure.field_count = layout.field_count;
        fields[1].offset = layout_fields[1].offset = 0u;
        check_false(cmeta_data_desc_valid(&data));
        fields[1].offset = layout_fields[1].offset = layout.size;
        check_false(cmeta_data_desc_valid(&data));
    }
    it("retains explicit data semantics at the sixteen-field replay boundary") {
        NativeWideRecord record;
        cmeta_object_ref object;
        const cmeta_data_desc *field_data = NULL;
        const void *value = NULL;
        const cmeta_data_desc *data = cmeta_reflected_data(NativeWideRecord);
        enum { LAST_FIELD_VALUE = 42, FIELD_LIMIT = 16 };
        check_equal(cmeta_data_value_init_zero(data, &record), CMETA_OK);
        record.f16 = LAST_FIELD_VALUE;
        check_equal(StructMeta(NativeWideRecord)->field_count, (size_t)FIELD_LIMIT);
        check_equal(cmeta_object_borrow(&object, &record, data, NULL), CMETA_OK);
        check_equal(cmeta_object_field_read(&object, "f16", &field_data, &value), CMETA_OK);
        check_true(field_data == &native_identifier_data);
        check_false(cmeta_data_desc_equal(field_data, &cmeta_data_int));
        check_equal(*(const int *)value, LAST_FIELD_VALUE);
        cmeta_object_release(&object);
    }
    it("reads an existing object through canonical reflection without mutation authority") {
        NativeRecord record = {7, {3, 4.5}, 9, true, 1.0f};
        cmeta_object_ref object;
        const cmeta_data_desc *value_data = NULL;
        const void *value = NULL;
        check_equal(cmeta_object_borrow(&object, &record,
            cmeta_reflected_data(NativeRecord), NULL), CMETA_OK);
        check_equal(cmeta_object_field_read(&object, "leaf", &value_data, &value), CMETA_OK);
        check_true(value == &record.leaf);
        check_true(cmeta_data_desc_equal(value_data, cmeta_reflected_data(NativeLeaf)));
        check_equal(cmeta_object_field_read(&object, "renamed", &value_data, &value), CMETA_OK);
        check_equal(*(const int *)value, record.renamed);
        check_equal(cmeta_object_field_assign(&object, "renamed", &cmeta_data_int,
            &record.leaf.count), CMETA_TRAIT_MISSING);
        check_equal(record.renamed, 9);
        cmeta_object_release(&object);
        check_equal(record.leaf.count, 3);
    }
    it("preserves explicit field identity and uses one field list for both views") {
        const cmeta_data_struct_shape *shape =
            (const cmeta_data_struct_shape *)cmeta_reflected_data(NativeRecord)->shape;
        const cmeta_data_field_desc *field = cmeta_data_struct_find_field(shape, "renamed");
        check_not_null(field);
        check_equal(field->stable_id, "test.native.Record.original");
        check_equal(shape->fields[0].stable_id, "test.native.Record.serial");
        check_equal(shape->field_count, StructMeta(NativeRecord)->field_count);
        for (size_t index = 0; index < shape->field_count; ++index) {
            const cmeta_field_desc *layout = cmeta_struct_field(shape->layout, index);
            check_equal(shape->fields[index].name, layout->name);
            check_equal(shape->fields[index].offset, layout->offset);
            check_equal(shape->fields[index].value->storage_type->size, layout->size);
        }
    }
    it("copies and restores nested values through existing semantic lifecycle APIs") {
        NativeRecord source = {7, {3, 4.5}, 9, true, 1.0f};
        NativeRecord destination;
        bool zero = false;
        const cmeta_data_desc *data = cmeta_reflected_data(NativeRecord);
        check_true(cmeta_data_desc_valid(data));
        check_equal(cmeta_data_value_init_zero(data, &destination), CMETA_OK);
        check_equal(cmeta_data_value_copy(data, &destination, &source), CMETA_OK);
        check_equal(destination.serial, source.serial);
        check_equal(destination.leaf.count, source.leaf.count);
        check_equal(destination.leaf.score, source.leaf.score);
        check_equal(destination.renamed, source.renamed);
        check_equal(destination.active, source.active);
        check_equal(destination.weight, source.weight);
        check_equal(cmeta_data_value_restore_zero(data, &destination), CMETA_OK);
        check_equal(cmeta_data_value_is_zero(data, &destination, &zero), CMETA_OK);
        check_true(zero);
        check_equal(source.leaf.count, 3);
    }
    it("compares equivalent descriptors across translation units semantically") {
        const cmeta_data_desc *local = cmeta_reflected_data(NativeRecord);
        const cmeta_data_desc *peer = cmeta_test_peer_record_data();
        check_true(cmeta_data_desc_valid(peer));
        check_true(cmeta_type_equal(local->storage_type, peer->storage_type));
        check_true(cmeta_data_desc_equal(local, peer));
    }
}
