#include "tinytest.h"

#include "mustache.h"
#include "mustache_xml.h"
#include "xml/cxparser.h"

#include <stdlib.h>
#include <string.h>

static char *render_xml_text(const char *template_text, const char *xml_text) {
  cxml_root_node *root = cxml_parse_xml(xml_text);
  MUSTACHE_TEMPLATE *templ = NULL;
  MUSTACHE_STRING_RENDERER renderer = {0};
  char *result = NULL;
  int renderer_ready = 0;

  if (!root) return NULL;
  templ = mustache_compile(template_text, strlen(template_text), NULL, NULL, 0);
  if (!templ) goto cleanup;
  if (mustache_string_renderer_init(&renderer) != 0) goto cleanup;
  renderer_ready = 1;
  if (mustache_render_xml(templ, root, &renderer.base, &renderer, NULL, NULL) != 0) goto cleanup;
  result = mustache_string_renderer_get(&renderer);

cleanup:
  if (renderer_ready) mustache_string_renderer_free(&renderer);
  mustache_release(templ);
  cxml_root_node_free(root);
  return result;
}

spec("mustache XML integration") {
  describe("name matching") {
    it("should preserve XML element and attribute case") {
      char *result = render_xml_text("{{Foo}}|{{foo}}|{{FOO}}|{{Name}}|{{name}}",
                                     "<root Name=\"Upper\" name=\"lower\">"
                                     "<Foo>A</Foo><foo>B</foo></root>");
      check_not_null(result);
      if (result) check_str_eq(result, "A|B||Upper|lower");
      free(result);
    }

    it("should not merge equal local names from different namespaces") {
      char *result = render_xml_text("{{a:item}}|{{b:item}}|{{item}}",
                                     "<root xmlns:a=\"urn:a\" xmlns:b=\"urn:b\">"
                                     "<a:item>A</a:item><b:item>B</b:item></root>");
      check_not_null(result);
      if (result) check_str_eq(result, "A|B|");
      free(result);
    }
  }

  describe("repeated elements") {
    it("should iterate exact-name siblings") {
      char *result = render_xml_text("{{#item}}{{.}},{{/item}}",
                                     "<root><item>A</item><item>B</item></root>");
      check_not_null(result);
      if (result) check_str_eq(result, "A,B,");
      free(result);
    }
  }

  describe("provider status") {
    it("should reject a missing provider") {
      check_int_ne(mustache_xml_provider_status(NULL), 0);
    }

    it("should report success after initialization") {
      cxml_root_node *root = cxml_parse_xml("<root/>");
      MUSTACHE_XML_PROVIDER provider;
      check_not_null(root);
      if (root) {
        check_int_eq(mustache_xml_provider_init(&provider, root, NULL, NULL), 0);
        check_int_eq(mustache_xml_provider_status(&provider), 0);
        mustache_xml_provider_free(&provider);
        cxml_root_node_free(root);
      }
    }
  }
}
