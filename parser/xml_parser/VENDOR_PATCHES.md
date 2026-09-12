# XML parser maintenance notes

## cxml local-name initialization

`vendor/cxml/src/query/cxqapi.c`, `cxml_set_name`: skip clearing the old local-name buffer when its length is zero. A newly allocated node has a null `qname` buffer; even a zero-length `memset` must not evaluate null-pointer arithmetic or pass null to an annotated nonnull parameter.

Reproduced in Native XML SAX run 34597251013 at head f30fb55a9588b0d7212e80449cac841953314ae3. The existing `xml_parser_test` case "builds and serializes a document through opaque handles" reaches `salts_xml_document_create` and reports UBSan at cxqapi.c:1547. This change keeps the existing rename path and removes only the undefined operation on a new name. No sanitizer suppression is used.

## Incremental SAX ownership

`Salts::XmlParser` owns both the document parser and the incremental lexical SAX parser exposed by `<xml_parser/xml_sax.h>`. SAX callback spans are borrowed for the callback duration. Attribute and text spans retain entity spelling; document binding uses the native DOM for decoded values. Each instance owns its diagnostic, open-element stack, and bounded pending-token buffer. Callback failure is terminal; a failed or finished instance is not reusable.

The SAX regression checks each input chunk size, callbacks before EOF, sticky callback failure, independent diagnostics, embedded NUL rejection, malformed documents, and explicit pending-buffer limits. The default DataBind consumer must not duplicate these parser implementations.

Run the exact committed source with `xml_parser_test` and `xml_sax_parser_test` under both ASan and UBSan. A successful SAX test alone does not establish a successful DOM test; both are required by `.github/workflows/xml-sax.yml`.
