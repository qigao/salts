/*
 * Copyright © 2021 Jeremiah Ikosin
 * Distributed under the terms of the MIT license.
 */

#include "cxfixture.h"

// todo: more comprehensive tests

cts test_cxml_xpath(){
    cxml_root_node *root = cxml_load_string(wf_xml_9);
    cxml_assert(root)
    cxml_set *nodeset = cxml_xpath(root, "//*");
    cxml_assert__not_null(nodeset)
    cxml_assert__two(cxml_set_size(nodeset))

    int i = 0;
    char *expected[] = {"fruit", "name"};
    cxml_for_each(node, &nodeset->items)
    {
        cxml_assert__eq(_cxml_node_type(node), CXML_ELEM_NODE)
        cxml_assert__true(cxml_string_raw_equals(
                &_unwrap__cxnode(elem, node)->name.qname, expected[i]))
        i++;
    }

    cxml_assert__null(cxml_xpath(root, NULL))
    cxml_assert__null(cxml_xpath(NULL, "//*"))
    // automatically cleans up all nodes, including in the nodeset
    cxml_destroy(root);
    cxml_set_free(nodeset);
    FREE(nodeset);
    cxml_pass()
}

cts test_cxml_xpath_qvm_expressions(){
    cxml_root_node *root = cxml_load_string(wf_xml_10);
    cxml_assert(root)

    cxml_set *nodeset = cxml_xpath(root, "/fruit/name[1 + 1 = 2]");
    cxml_assert__not_null(nodeset)
    cxml_assert__two(cxml_set_size(nodeset))
    cxml_set_free(nodeset);
    FREE(nodeset);

    nodeset = cxml_xpath(root, "/fruit/name[-1 < 0 and text() = 'banana']");
    cxml_assert__not_null(nodeset)
    cxml_assert__one(cxml_set_size(nodeset))
    cxml_elem_node *name = cxml_set_get(nodeset, 0);
    cxml_assert__true(cxml_string_raw_equals(&name->name.qname, "name"))
    cxml_set_free(nodeset);
    FREE(nodeset);

    cxml_destroy(root);
    cxml_pass()
}

cts test_cxml_xpath_dialect_extensions(){
    cxml_root_node *root = cxml_load_string(wf_xml_10);
    cxml_assert(root)
    cxml_set *nodeset;

    /* idiv truncates toward zero: 5 idiv 2 == 2 */
    nodeset = cxml_xpath(root, "/fruit/name[5 idiv 2 = 2]");
    cxml_assert__not_null(nodeset)
    cxml_assert__two(cxml_set_size(nodeset))
    cxml_set_free(nodeset);
    FREE(nodeset);

    /* idiv is distinct from div: 5 div 2 == 2.5 makes the predicate false */
    nodeset = cxml_xpath(root, "/fruit/name[5 div 2 = 2]");
    cxml_assert__not_null(nodeset)
    cxml_assert__zero(cxml_set_size(nodeset))
    cxml_set_free(nodeset);
    FREE(nodeset);

    /* matches() with literal strings */
    nodeset = cxml_xpath(root, "/fruit/name[matches('banana', 'ana')]");
    cxml_assert__not_null(nodeset)
    cxml_assert__two(cxml_set_size(nodeset))
    cxml_set_free(nodeset);
    FREE(nodeset);

    nodeset = cxml_xpath(root, "/fruit/name[matches('apple', '^a')]");
    cxml_assert__not_null(nodeset)
    cxml_assert__two(cxml_set_size(nodeset))
    cxml_set_free(nodeset);
    FREE(nodeset);

    /* matches() against node text: banana matches 'ana', apple does not */
    nodeset = cxml_xpath(root, "/fruit/name[matches(text(), 'ana')]");
    cxml_assert__not_null(nodeset)
    cxml_assert__one(cxml_set_size(nodeset))
    cxml_set_free(nodeset);
    FREE(nodeset);

    /* ends-with() with literal strings */
    nodeset = cxml_xpath(root, "/fruit/name[ends-with('apple', 'e')]");
    cxml_assert__not_null(nodeset)
    cxml_assert__two(cxml_set_size(nodeset))
    cxml_set_free(nodeset);
    FREE(nodeset);

    /* ends-with() against node text: banana ends with 'na', apple with 'le' */
    nodeset = cxml_xpath(root, "/fruit/name[ends-with(text(), 'na')]");
    cxml_assert__not_null(nodeset)
    cxml_assert__one(cxml_set_size(nodeset))
    cxml_set_free(nodeset);
    FREE(nodeset);

    nodeset = cxml_xpath(root, "/fruit/name[ends-with(text(), 'le')]");
    cxml_assert__not_null(nodeset)
    cxml_assert__one(cxml_set_size(nodeset))
    cxml_set_free(nodeset);
    FREE(nodeset);

    cxml_destroy(root);
    cxml_pass()
}

cts test_cxml_xpath_if_expr(){
    cxml_root_node *root = cxml_load_string(wf_xml_10);
    cxml_assert(root)
    cxml_set *nodeset;

    /* constant condition selects the true branch */
    nodeset = cxml_xpath(root, "/fruit/name[if (1 = 1) then true() else false()]");
    cxml_assert__not_null(nodeset)
    cxml_assert__two(cxml_set_size(nodeset))
    cxml_set_free(nodeset);
    FREE(nodeset);

    /* constant condition selects the false branch */
    nodeset = cxml_xpath(root, "/fruit/name[if (0 = 1) then false() else true()]");
    cxml_assert__not_null(nodeset)
    cxml_assert__two(cxml_set_size(nodeset))
    cxml_set_free(nodeset);
    FREE(nodeset);

    /* condition from node text: only banana matches 'ana' */
    nodeset = cxml_xpath(root, "/fruit/name[if (matches(text(), 'ana')) then true() else false()]");
    cxml_assert__not_null(nodeset)
    cxml_assert__one(cxml_set_size(nodeset))
    cxml_set_free(nodeset);
    FREE(nodeset);

    /* ends-with drives the branch: only apple ends with 'e' */
    nodeset = cxml_xpath(root, "/fruit/name[if (ends-with(text(), 'e')) then 1 = 1 else false()]");
    cxml_assert__not_null(nodeset)
    cxml_assert__one(cxml_set_size(nodeset))
    cxml_set_free(nodeset);
    FREE(nodeset);

    /* lazy evaluation: the else branch is never evaluated, so the
     * division-by-zero there must not raise an error */
    nodeset = cxml_xpath(root, "/fruit/name[if (true()) then true() else (5 idiv 0 = 0)]");
    cxml_assert__not_null(nodeset)
    cxml_assert__two(cxml_set_size(nodeset))
    cxml_set_free(nodeset);
    FREE(nodeset);

    cxml_destroy(root);
    cxml_pass()
}

void suite_cxxpath(){
    cxml_suite(cxxpath)
    {
        cxml_add_test(test_cxml_xpath)
        cxml_add_test(test_cxml_xpath_qvm_expressions)
        cxml_add_test(test_cxml_xpath_dialect_extensions)
        cxml_add_test(test_cxml_xpath_if_expr)
        cxml_run_suite()
    }
}
