#include <xml_parser/xml_parser.h>

#include "core/cxmem.h"

#include "tinytest.h"

#include <stddef.h>
#include <string.h>

static turbo_xml_location expected_location(const char *input,
                                            const char *position) {
    turbo_xml_location location = {0u, 1u, 1u};
    const char *cursor;

    location.byte_offset = (size_t)(position - input);
    for (cursor = input; cursor < position; ++cursor) {
        if (*cursor == '\n') {
            ++location.line;
            location.column = 1u;
        } else {
            ++location.column;
        }
    }
    return location;
}

static void check_location_equal(turbo_xml_location actual,
                                 turbo_xml_location expected) {
    check_equal(actual.byte_offset, expected.byte_offset);
    check_equal(actual.line, expected.line);
    check_equal(actual.column, expected.column);
}

static void check_view(turbo_xml_string_view actual, const char *expected) {
    const size_t expected_size = strlen(expected);
    check_equal(actual.size, expected_size);
    check_not_null(actual.data);
    check_equal(memcmp(actual.data, expected, expected_size), 0);
}

static void check_retained_limit_releases(const char *source,
                                          size_t maximum_bytes) {
    const size_t allocations_before = cxml_test_outstanding_allocations();
    turbo_xml_limits limits = turbo_xml_default_limits();
    turbo_xml_document document = {0};
    turbo_xml_diagnostic diagnostic = {0};

    limits.max_retained_string_bytes = maximum_bytes;
    check_equal(turbo_xml_parse(&document, source, strlen(source), &limits,
                                &diagnostic),
                TURBO_XML_LIMIT_EXCEEDED);
    check_null(document.impl);
    check_equal(cxml_test_outstanding_allocations(), allocations_before);
}

static void check_allocation_failures_release_everything(const char *source) {
    enum { MAX_ALLOCATION_FAILURE_POINTS = 512 };
    size_t failure_point;
    bool reached_success = false;

    for (failure_point = 0u;
         failure_point < (size_t)MAX_ALLOCATION_FAILURE_POINTS;
         ++failure_point) {
        const size_t allocations_before = cxml_test_outstanding_allocations();
        turbo_xml_document document = {0};
        turbo_xml_diagnostic diagnostic = {0};
        turbo_xml_status status;

        cxml_test_fail_allocation_after(failure_point);
        status = turbo_xml_parse(&document, source, strlen(source), NULL,
                                 &diagnostic);
        cxml_test_clear_allocation_failure();

        if (status == TURBO_XML_OK) {
            turbo_xml_document_destroy(&document);
            check_equal(cxml_test_outstanding_allocations(),
                        allocations_before);
            reached_success = true;
            break;
        }

        check_equal(status, TURBO_XML_ALLOCATION_FAILED);
        check_null(document.impl);
        check_equal(diagnostic.status, TURBO_XML_ALLOCATION_FAILED);
        check(cxml_test_outstanding_allocations() == allocations_before,
              "allocation failure point %zu leaked cxml storage: %s",
              failure_point, diagnostic.message);
    }

    check_true(reached_success);
}

suite("bounded XML parser facade") {
    it("maps private cxml allocation failure without terminating the process") {
        static const char source[] = "<root><child/></root>";
        turbo_xml_document document = {0};
        turbo_xml_diagnostic diagnostic = {0};

        cxml_test_fail_allocation_after(0u);
        check_equal(turbo_xml_parse(&document, source, strlen(source), NULL,
                                    &diagnostic),
                    TURBO_XML_ALLOCATION_FAILED);
        cxml_test_clear_allocation_failure();
        check_null(document.impl);
        check_equal(diagnostic.status, TURBO_XML_ALLOCATION_FAILED);
    }

    it("releases the partial DOM at every cxml allocation failure point") {
        static const char source[] =
            "<?xml version='1.0' encoding='UTF-8'?>"
            "<root xmlns:p='urn:test' a='1' b='2'>"
            "<?work item?><p:child c='3'>text</p:child>"
            "</root>";

        check_allocation_failures_release_everything("<root/>");
        check_allocation_failures_release_everything(
            "<?xml version='1.0' encoding='UTF-8'?><root/>");
        check_allocation_failures_release_everything(
            "<root xmlns:p='urn:test' a='1' b='2'/>");
        check_allocation_failures_release_everything(
            "<root><?work item?></root>");
        check_allocation_failures_release_everything(
            "<root><!--comment--></root>");
        check_allocation_failures_release_everything(
            "<root xmlns:p='urn:test' p:a='value'/>");
        check_allocation_failures_release_everything(
            "<root a0='0' a1='1' a2='2' a3='3' a4='4' a5='5' a6='6'/>");
        check_allocation_failures_release_everything(
            "<root><p:child xmlns:p='urn:test' c='3'>text</p:child></root>");
        check_allocation_failures_release_everything(source);
    }

    it("retains namespace-aware DOM views and exact source locations") {
        static const char xml[] =
            "<root xmlns='urn:root'>\n"
            "  <p:item xmlns:p='urn:item' p:key='value'>text</p:item>\n"
            "</root>";
        turbo_xml_document document = {0};
        turbo_xml_diagnostic diagnostic = {0};
        turbo_xml_node root;
        turbo_xml_node item;
        turbo_xml_node text;
        turbo_xml_attribute key = {0};
        size_t index;

        check_equal(turbo_xml_parse(&document, xml, sizeof(xml) - 1u,
                                    NULL, &diagnostic),
                    TURBO_XML_OK);
        check_not_null(document.impl);

        root = turbo_xml_document_root(&document);
        check_not_null(root.impl);
        check_equal(turbo_xml_node_type(root), TURBO_XML_ELEMENT);
        check_view(turbo_xml_node_qualified_name(root), "root");
        check_view(turbo_xml_node_local_name(root), "root");
        check_view(turbo_xml_node_namespace_uri(root), "urn:root");
        check_location_equal(turbo_xml_node_location(root),
                             expected_location(xml, strstr(xml, "<root")));

        check_equal(turbo_xml_node_child_count(root), (size_t)1u);
        item = turbo_xml_node_child_at(root, 0u);
        check_equal(turbo_xml_node_type(item), TURBO_XML_ELEMENT);
        check_view(turbo_xml_node_qualified_name(item), "p:item");
        check_view(turbo_xml_node_local_name(item), "item");
        check_view(turbo_xml_node_namespace_uri(item), "urn:item");
        check_location_equal(turbo_xml_node_location(item),
                             expected_location(xml, strstr(xml, "<p:item")));

        for (index = 0u; index < turbo_xml_node_attribute_count(item); ++index) {
            turbo_xml_attribute candidate =
                turbo_xml_node_attribute_at(item, index);
            turbo_xml_string_view name =
                turbo_xml_attribute_qualified_name(candidate);
            if (name.size == 5u && memcmp(name.data, "p:key", 5u) == 0) {
                key = candidate;
                break;
            }
        }
        check_not_null(key.impl);
        check_view(turbo_xml_attribute_local_name(key), "key");
        check_view(turbo_xml_attribute_namespace_uri(key), "urn:item");
        check_view(turbo_xml_attribute_value(key), "value");
        check_location_equal(turbo_xml_attribute_location(key),
                             expected_location(xml, strstr(xml, "p:key")));

        check_equal(turbo_xml_node_child_count(item), (size_t)1u);
        text = turbo_xml_node_child_at(item, 0u);
        check_equal(turbo_xml_node_type(text), TURBO_XML_TEXT);
        check_view(turbo_xml_node_value(text), "text");

        turbo_xml_document_destroy(&document);
        check_null(document.impl);
        turbo_xml_document_destroy(&document);
    }

    it("serializes bounded mixed child fragments without exposing cxml") {
        static const char xml[] =
            "<root> lead <p:item xmlns:p='urn:item' p:key='a&amp;b'>"
            "x&lt;y</p:item> tail <!--note--><?work value?></root>";
        static const char expected[] =
            " lead <p:item xmlns:p=\"urn:item\" p:key=\"a&amp;b\">"
            "x&lt;y</p:item> tail <!--note--><?work value?>";
        turbo_xml_document document = {0};
        turbo_xml_diagnostic diagnostic = {0};
        turbo_xml_node root;
        char output[256] = {0};
        size_t size = 0u;

        check_equal(turbo_xml_parse(&document, xml, sizeof(xml) - 1u,
                                    NULL, &diagnostic),
                    TURBO_XML_OK);
        root = turbo_xml_document_root(&document);
        check_equal(turbo_xml_serialize_children(
                        root, NULL, 0u, sizeof(output) - 1u, &size),
                    TURBO_XML_OK);
        check_equal(size, sizeof(expected) - 1u);
        check_equal(turbo_xml_serialize_children(
                        root, output, size + 1u,
                        sizeof(output) - 1u, &size),
                    TURBO_XML_OK);
        check_equal(output, expected, sizeof(expected));
        check_equal(turbo_xml_serialize_children(
                        root, output, size, sizeof(output) - 1u, &size),
                    TURBO_XML_LIMIT_EXCEEDED);
        check_equal(turbo_xml_serialize_children(
                        root, NULL, 0u, size - 1u, &size),
                    TURBO_XML_LIMIT_EXCEEDED);
        turbo_xml_document_destroy(&document);
    }

    it("serializes empty and whitespace-only child fragments exactly") {
        static const char empty_xml[] = "<root/>";
        static const char whitespace_xml[] = "<root> \n\t </root>";
        turbo_xml_document document = {0};
        turbo_xml_diagnostic diagnostic = {0};
        char output[16] = {0};
        size_t size = SIZE_MAX;

        check_equal(turbo_xml_parse(&document, empty_xml,
                                    sizeof(empty_xml) - 1u,
                                    NULL, &diagnostic), TURBO_XML_OK);
        check_equal(turbo_xml_serialize_children(
                        turbo_xml_document_root(&document), output,
                        sizeof(output), sizeof(output) - 1u, &size),
                    TURBO_XML_OK);
        check_equal(size, (size_t)0u);
        check_equal(output, "", sizeof(""));
        turbo_xml_document_destroy(&document);

        check_equal(turbo_xml_parse(&document, whitespace_xml,
                                    sizeof(whitespace_xml) - 1u,
                                    NULL, &diagnostic), TURBO_XML_OK);
        check_equal(turbo_xml_serialize_children(
                        turbo_xml_document_root(&document), output,
                        sizeof(output), sizeof(output) - 1u, &size),
                    TURBO_XML_OK);
        check_equal(output, " \n\t ", sizeof(" \n\t "));
        turbo_xml_document_destroy(&document);
    }

    it("reports the first malformed token location without publishing") {
        static const char xml[] = "<root>\n  <child>\n</root>";
        turbo_xml_document document = {0};
        turbo_xml_diagnostic diagnostic = {0};

        check_equal(turbo_xml_parse(&document, xml, sizeof(xml) - 1u,
                                    NULL, &diagnostic),
                    TURBO_XML_MALFORMED);
        check_null(document.impl);
        check_equal(diagnostic.status, TURBO_XML_MALFORMED);
        check_location_equal(diagnostic.location,
                             expected_location(xml, strrchr(xml, '<') + 2));
        check_contains(diagnostic.message, "Closing tag");
    }

    it("retains multiline comment CDATA and PI token locations") {
        static const char xml[] =
            "<r>\r\n"
            "<!-- first\nsecond -->\n"
            "<![CDATA[value\nline]]>\n"
            "<?work value?>\n"
            "</r>";
        turbo_xml_document document = {0};
        turbo_xml_diagnostic diagnostic = {0};
        turbo_xml_node root;
        turbo_xml_node comment;
        turbo_xml_node cdata;
        turbo_xml_node instruction;

        check_equal(turbo_xml_parse(&document, xml, sizeof(xml) - 1u, NULL,
                                    &diagnostic),
                    TURBO_XML_OK);
        root = turbo_xml_document_root(&document);
        check_equal(turbo_xml_node_child_count(root), (size_t)3u);
        comment = turbo_xml_node_child_at(root, 0u);
        cdata = turbo_xml_node_child_at(root, 1u);
        instruction = turbo_xml_node_child_at(root, 2u);
        check_equal(turbo_xml_node_type(comment), TURBO_XML_COMMENT);
        check_equal(turbo_xml_node_type(cdata), TURBO_XML_TEXT);
        check_equal(turbo_xml_node_type(instruction),
                    TURBO_XML_PROCESSING_INSTRUCTION);
        check_location_equal(turbo_xml_node_location(comment),
                             expected_location(xml, strstr(xml, "<!--")));
        check_location_equal(turbo_xml_node_location(cdata),
                             expected_location(xml, strstr(xml, "<![CDATA[")));
        check_location_equal(turbo_xml_node_location(instruction),
                             expected_location(xml, strstr(xml, "work")));
        turbo_xml_document_destroy(&document);
    }

    it("rejects embedded NUL before invoking the XML engine") {
        static const char xml[] = {'<', 'r', '/', '>', '\0', '<', 'x', '/', '>'};
        turbo_xml_document document = {0};
        turbo_xml_diagnostic diagnostic = {0};

        check_equal(turbo_xml_parse(&document, xml, sizeof(xml), NULL,
                                    &diagnostic),
                    TURBO_XML_EMBEDDED_NUL);
        check_null(document.impl);
        check_equal(diagnostic.location.byte_offset, (size_t)4u);
        check_equal(diagnostic.location.line, (uint32_t)1u);
        check_equal(diagnostic.location.column, (uint32_t)5u);
    }

    it("accepts exact limits and rejects each limit plus one") {
        static const char xml[] = "<r a='1'><x>v</x></r>";
        turbo_xml_limits limits = turbo_xml_default_limits();
        turbo_xml_document document = {0};
        turbo_xml_diagnostic diagnostic = {0};

        limits.max_input_bytes = sizeof(xml) - 1u;
        limits.max_nodes = 3u;
        limits.max_attributes = 1u;
        limits.max_depth = 2u;
        limits.max_retained_string_bytes = 5u;
        check_equal(turbo_xml_parse(&document, xml, sizeof(xml) - 1u,
                                    &limits, &diagnostic),
                    TURBO_XML_OK);
        turbo_xml_document_destroy(&document);

        limits.max_input_bytes = sizeof(xml) - 2u;
        check_equal(turbo_xml_parse(&document, xml, sizeof(xml) - 1u,
                                    &limits, &diagnostic),
                    TURBO_XML_LIMIT_EXCEEDED);
        check_null(document.impl);

        limits = turbo_xml_default_limits();
        limits.max_nodes = 2u;
        check_equal(turbo_xml_parse(&document, xml, sizeof(xml) - 1u,
                                    &limits, &diagnostic),
                    TURBO_XML_LIMIT_EXCEEDED);

        limits = turbo_xml_default_limits();
        limits.max_attributes = 0u;
        check_equal(turbo_xml_parse(&document, xml, sizeof(xml) - 1u,
                                    &limits, &diagnostic),
                    TURBO_XML_INVALID_ARGUMENT);

        limits = turbo_xml_default_limits();
        limits.max_attributes = 1u;
        check_equal(turbo_xml_parse(&document, "<r a='1' b='2'/>",
                                    strlen("<r a='1' b='2'/>"), &limits,
                                    &diagnostic),
                    TURBO_XML_LIMIT_EXCEEDED);

        limits = turbo_xml_default_limits();
        limits.max_depth = 1u;
        check_equal(turbo_xml_parse(&document, xml, sizeof(xml) - 1u,
                                    &limits, &diagnostic),
                    TURBO_XML_LIMIT_EXCEEDED);

        limits = turbo_xml_default_limits();
        limits.max_retained_string_bytes = 4u;
        check_equal(turbo_xml_parse(&document, xml, sizeof(xml) - 1u,
                                    &limits, &diagnostic),
                    TURBO_XML_LIMIT_EXCEEDED);
    }

    it("accounts for XML declarations and rejects invalid declaration order") {
        static const char declaration[] =
            "<?xml version='1.0' encoding='UTF-8' standalone='yes'?><r a='1'/>";
        static const char wrong_order[] =
            "<?xml encoding='UTF-8' version='1.0'?><r/>";
        static const char unknown_field[] =
            "<?xml version='1.0' vendor='x'?><r/>";
        static const char invalid_version[] =
            "<?xml version='2.0'?><r/>";
        static const char invalid_encoding[] =
            "<?xml version='1.0' encoding='9-bit'?><r/>";
        static const char invalid_standalone[] =
            "<?xml version='1.0' standalone='true'?><r/>";
        static const char after_pi[] =
            "<?work item?><?xml version='1.0'?><r/>";
        static const char after_whitespace[] =
            " <?xml version='1.0'?><r/>";
        static const char duplicate[] =
            "<?xml version='1.0'?><?xml version='1.0'?><r/>";
        static const char after_root[] =
            "<r/><?xml version='1.0'?>";
        static const char uppercase_target[] =
            "<?XML version='1.0'?><r/>";
        static const char bom_declaration[] =
            "\xEF\xBB\xBF<?xml version='1.0'?><r/>";
        static const char bom_then_whitespace[] =
            "\xEF\xBB\xBF <?xml version='1.0'?><r/>";
        turbo_xml_limits limits = turbo_xml_default_limits();
        turbo_xml_document document = {0};
        turbo_xml_diagnostic diagnostic = {0};

        check_equal(turbo_xml_parse(&document, declaration,
                                    sizeof(declaration) - 1u, NULL,
                                    &diagnostic),
                    TURBO_XML_OK);
        turbo_xml_document_destroy(&document);

        limits.max_nodes = 1u;
        check_equal(turbo_xml_parse(&document, declaration,
                                    sizeof(declaration) - 1u, &limits,
                                    &diagnostic),
                    TURBO_XML_LIMIT_EXCEEDED);
        check_null(document.impl);

        limits = turbo_xml_default_limits();
        limits.max_attributes = 3u;
        check_equal(turbo_xml_parse(&document, declaration,
                                    sizeof(declaration) - 1u, &limits,
                                    &diagnostic),
                    TURBO_XML_LIMIT_EXCEEDED);

        limits = turbo_xml_default_limits();
        limits.max_retained_string_bytes = 1u;
        check_equal(turbo_xml_parse(&document, declaration,
                                    sizeof(declaration) - 1u, &limits,
                                    &diagnostic),
                    TURBO_XML_LIMIT_EXCEEDED);

        check_equal(turbo_xml_parse(&document, wrong_order,
                                    sizeof(wrong_order) - 1u, NULL,
                                    &diagnostic),
                    TURBO_XML_MALFORMED);
        check_equal(turbo_xml_parse(&document, unknown_field,
                                    sizeof(unknown_field) - 1u, NULL,
                                    &diagnostic),
                    TURBO_XML_MALFORMED);
        check_equal(turbo_xml_parse(&document, invalid_version,
                                    sizeof(invalid_version) - 1u, NULL,
                                    &diagnostic),
                    TURBO_XML_MALFORMED);
        check_equal(turbo_xml_parse(&document, invalid_encoding,
                                    sizeof(invalid_encoding) - 1u, NULL,
                                    &diagnostic),
                    TURBO_XML_MALFORMED);
        check_equal(turbo_xml_parse(&document, invalid_standalone,
                                    sizeof(invalid_standalone) - 1u, NULL,
                                    &diagnostic),
                    TURBO_XML_MALFORMED);
        check_equal(turbo_xml_parse(&document, after_pi,
                                    sizeof(after_pi) - 1u, NULL,
                                    &diagnostic),
                    TURBO_XML_MALFORMED);
        check_equal(turbo_xml_parse(&document, after_whitespace,
                                    sizeof(after_whitespace) - 1u, NULL,
                                    &diagnostic),
                    TURBO_XML_MALFORMED);
        check_equal(turbo_xml_parse(&document, duplicate,
                                    sizeof(duplicate) - 1u, NULL,
                                    &diagnostic),
                    TURBO_XML_MALFORMED);
        check_equal(turbo_xml_parse(&document, after_root,
                                    sizeof(after_root) - 1u, NULL,
                                    &diagnostic),
                    TURBO_XML_MALFORMED);
        check_equal(turbo_xml_parse(&document, uppercase_target,
                                    sizeof(uppercase_target) - 1u, NULL,
                                    &diagnostic),
                    TURBO_XML_MALFORMED);
        check_equal(turbo_xml_parse(&document, bom_declaration,
                                    sizeof(bom_declaration) - 1u, NULL,
                                    &diagnostic),
                    TURBO_XML_OK);
        turbo_xml_document_destroy(&document);
        check_equal(turbo_xml_parse(&document, bom_then_whitespace,
                                    sizeof(bom_then_whitespace) - 1u, NULL,
                                    &diagnostic),
                    TURBO_XML_MALFORMED);
    }

    it("releases every unowned node when retained-string admission fails") {
        check_retained_limit_releases("<long/>", 1u);
        check_retained_limit_releases("<r a='value'/>", 2u);
        check_retained_limit_releases("<r xmlns:p='uri'/>", 2u);
        check_retained_limit_releases("<r><?pi value?></r>", 3u);
        check_retained_limit_releases("<?xml version='1.0'?><r/>", 7u);
    }

    it("enforces structural budgets before building the remaining DOM") {
        char xml[512];
        static const char child[] = "<x/>";
        turbo_xml_limits limits = turbo_xml_default_limits();
        turbo_xml_document document = {0};
        turbo_xml_diagnostic diagnostic = {0};
        size_t size = 0u;
        size_t index;

        memcpy(xml + size, "<r>", 3u);
        size += 3u;
        for (index = 0u; index < 64u; ++index) {
            memcpy(xml + size, child, sizeof(child) - 1u);
            size += sizeof(child) - 1u;
        }
        memcpy(xml + size, "</r>", 4u);
        size += 4u;

        limits.max_nodes = 1u;
        cxml_test_fail_allocation_after(64u);
        check_equal(turbo_xml_parse(&document, xml, size, &limits,
                                    &diagnostic),
                    TURBO_XML_LIMIT_EXCEEDED);
        cxml_test_clear_allocation_failure();
        check_null(document.impl);
        check_contains(diagnostic.message, "max_nodes");
    }

    it("builds and serializes a document through opaque handles") {
        turbo_xml_document document = {0};
        turbo_xml_node root;
        turbo_xml_node name = {0};
        char *serialized;
        size_t serialized_size = 0u;

        check_equal(turbo_xml_document_create(&document, "Item"), TURBO_XML_OK);
        root = turbo_xml_document_root(&document);
        check_equal(turbo_xml_node_add_element(root, "name", &name), TURBO_XML_OK);
        check_equal(turbo_xml_node_set_text(name, "turbo & utils"), TURBO_XML_OK);
        serialized = turbo_xml_document_serialize(&document, &serialized_size);
        check_not_null(serialized);
        if (serialized) {
            check_contains(serialized, "<Item>");
            check_contains(serialized, "turbo &amp; utils");
            check_equal(serialized_size, strlen(serialized));
        }
        turbo_xml_owned_string_free(serialized);
        turbo_xml_document_destroy(&document);
    }

    it("returns owned lists of borrowed query and XPath nodes") {
        static const char xml[] = "<fruit><name>banana</name><name>pear</name></fruit>";
        turbo_xml_document document = {0};
        turbo_xml_diagnostic diagnostic = {0};
        turbo_xml_node_list nodes = {0};
        turbo_xml_node found;

        check_equal(turbo_xml_parse(&document, xml, sizeof(xml) - 1u, NULL, &diagnostic),
                    TURBO_XML_OK);
        found = turbo_xml_node_find(turbo_xml_document_root(&document), "<name>/");
        check_not_null(found.impl);
        check_equal(turbo_xml_node_find_all(turbo_xml_document_root(&document), "<name>/", &nodes),
                    TURBO_XML_OK);
        check_equal(turbo_xml_node_list_size(&nodes), (size_t)2u);
        turbo_xml_node_list_destroy(&nodes);

        check_equal(turbo_xml_document_xpath_query(&document, "/fruit/name", &nodes, NULL, NULL),
                    TURBO_XML_OK);
        check_equal(turbo_xml_node_list_size(&nodes), (size_t)2u);
        check_equal(turbo_xml_node_type(turbo_xml_node_list_at(&nodes, 0u)),
                    TURBO_XML_ELEMENT);
        turbo_xml_node_list_destroy(&nodes);
        turbo_xml_document_destroy(&document);
    }
}
