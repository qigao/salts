/**
 * @file mustache_xml.c
 * @brief XML data provider implementation for Mustache4C using cxml
 */

#include "mustache_xml.h"
#include "core/cxdefs.h"
#include "core/cxstr.h"
#include "core/cxlist.h"
#include "core/cxtable.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SURROGATE_LIST_TYPE 100
#define MUSTACHE_XML_PROVIDER_ERROR_CAPACITY SIZE_MAX

typedef struct {
    int _type;
    size_t count;
    void **items;
} SURROGATE_LIST;

/* Forward declarations */
static int xml_dump(void *node, int (*out_fn)(const char *, size_t, void *), void *renderer_data,
                    void *provider_data);
static void *xml_get_root(void *provider_data);
static void *xml_get_child_by_name(void *node, const char *name, size_t size, void *provider_data);
static void *xml_get_child_by_index(void *node, unsigned index, void *provider_data);
static MUSTACHE_TEMPLATE *xml_get_partial(const char *name, size_t size, void *provider_data);
static int xml_provider_failed(const MUSTACHE_XML_PROVIDER *provider);

int mustache_xml_provider_init(MUSTACHE_XML_PROVIDER *provider, void *xml_node,
                                MUSTACHE_TEMPLATE *(*template_loader)(const char *, size_t, void *),
                                void *user_data) {
    if (!provider || !xml_node) {
        return -1;
    }

    provider->base.dump = xml_dump;
    provider->base.get_root = xml_get_root;
    provider->base.get_child_by_name = xml_get_child_by_name;
    provider->base.get_child_by_index = xml_get_child_by_index;
    provider->base.get_partial = xml_get_partial;
    provider->base.is_lambda = NULL;
    provider->base.call_lambda = NULL;

    provider->root_node = xml_node;
    provider->template_loader = template_loader;
    provider->user_data = user_data;
    
    provider->allocated_lists = NULL;
    provider->list_count = 0;
    provider->list_capacity = 0;

    return 0;
}

void mustache_xml_provider_free(MUSTACHE_XML_PROVIDER *provider) {
    if (!provider) return;
    for (size_t i = 0; i < provider->list_count; i++) {
        SURROGATE_LIST *list = (SURROGATE_LIST *)provider->allocated_lists[i];
        if (list->items) free(list->items);
        free(list);
    }
    if (provider->allocated_lists) free(provider->allocated_lists);
    provider->allocated_lists = NULL;
    provider->list_count = 0;
    provider->list_capacity = 0;
}

int mustache_xml_provider_status(const MUSTACHE_XML_PROVIDER *provider) {
    return !provider || xml_provider_failed(provider) ? -1 : 0;
}

static int xml_provider_failed(const MUSTACHE_XML_PROVIDER *provider) {
    return provider && provider->list_capacity == MUSTACHE_XML_PROVIDER_ERROR_CAPACITY;
}

static int xml_name_eq(const char *candidate, tstr_v expected) {
    return candidate && tstr_v_eq(tstr_v_from_cstr(candidate), expected);
}

static void *add_surrogate(MUSTACHE_XML_PROVIDER *p, size_t count) {
    size_t new_capacity;
    void **new_lists;
    SURROGATE_LIST *list;

    if (!p || count == 0 || xml_provider_failed(p)) return NULL;
    if (p->list_count >= p->list_capacity) {
        if (p->list_capacity == 0) {
            new_capacity = 8;
        } else {
            if (p->list_capacity > SIZE_MAX / 2) {
                p->list_capacity = MUSTACHE_XML_PROVIDER_ERROR_CAPACITY;
                return NULL;
            }
            new_capacity = p->list_capacity * 2;
        }
        if (new_capacity > SIZE_MAX / sizeof(void *)) {
            p->list_capacity = MUSTACHE_XML_PROVIDER_ERROR_CAPACITY;
            return NULL;
        }
        new_lists = realloc(p->allocated_lists, new_capacity * sizeof(void *));
        if (!new_lists) {
            p->list_capacity = MUSTACHE_XML_PROVIDER_ERROR_CAPACITY;
            return NULL;
        }
        p->allocated_lists = new_lists;
        p->list_capacity = new_capacity;
    }
    list = malloc(sizeof(SURROGATE_LIST));
    if (!list) {
        p->list_capacity = MUSTACHE_XML_PROVIDER_ERROR_CAPACITY;
        return NULL;
    }
    list->_type = SURROGATE_LIST_TYPE;
    list->count = count;
    if (count > SIZE_MAX / sizeof(void *)) {
        free(list);
        p->list_capacity = MUSTACHE_XML_PROVIDER_ERROR_CAPACITY;
        return NULL;
    }
    list->items = malloc(count * sizeof(void *));
    if (!list->items) {
        free(list);
        p->list_capacity = MUSTACHE_XML_PROVIDER_ERROR_CAPACITY;
        return NULL;
    }
    p->allocated_lists[p->list_count++] = list;
    return list;
}

static int is_surrogate(void *node) {
    if (!node) return 0;
    /* Basic safety check for surrogate lists */
    SURROGATE_LIST *list = (SURROGATE_LIST *)node;
    return list->_type == SURROGATE_LIST_TYPE;
}

static int xml_dump(void *node, int (*out_fn)(const char *, size_t, void *), void *renderer_data,
                    void *provider_data) {
    if (!node) return 0;

    if (is_surrogate(node)) {
        return out_fn("[list]", 6, renderer_data);
    }

    _cxml_node_t type = _cxml_get_node_type(node);
    switch (type) {
        case CXML_ATTR_NODE: {
            cxml_attr_node *attr = (cxml_attr_node *)node;
            const char *raw = cxml_string_as_raw(&attr->value);
            return out_fn(raw, strlen(raw), renderer_data);
        }
        case CXML_TEXT_NODE: {
            cxml_text_node *text = (cxml_text_node *)node;
            const char *raw = cxml_string_as_raw(&text->value);
            return out_fn(raw, strlen(raw), renderer_data);
        }
        case CXML_ELEM_NODE: {
            cxml_elem_node *elem = (cxml_elem_node *)node;
            /* If it has no element children, dump its text content. Otherwise [object] */
            bool has_elem_children = false;
            cxml_for_each(child, &elem->children) {
                if (_cxml_get_node_type(child) == CXML_ELEM_NODE) {
                    has_elem_children = true;
                    break;
                }
            }

            if (!has_elem_children) {
                int res = 0;
                cxml_for_each(text_child, &elem->children) {
                    if (_cxml_get_node_type(text_child) == CXML_TEXT_NODE) {
                        cxml_text_node *t = (cxml_text_node *)text_child;
                        const char *raw = cxml_string_as_raw(&t->value);
                        if (raw) {
                            res = out_fn(raw, strlen(raw), renderer_data);
                            if (res != 0) return res;
                        }
                    }
                }
                return 0;
            }
            return out_fn("[object]", 8, renderer_data);
        }
        default:
            return 0;
    }
}

static void *xml_get_root(void *provider_data) {
    MUSTACHE_XML_PROVIDER *provider = (MUSTACHE_XML_PROVIDER *)provider_data;
    if (provider->root_node && _cxml_get_node_type(provider->root_node) == CXML_ROOT_NODE) {
        return ((cxml_root_node *)provider->root_node)->root_element;
    }
    return provider->root_node;
}

static void *xml_get_child_by_name(void *node, const char *name, size_t size, void *provider_data) {
    MUSTACHE_XML_PROVIDER *p = (MUSTACHE_XML_PROVIDER *)provider_data;
    tstr_v expected = tstr_v_from_buf(name, size);
    if (!node || !name || is_surrogate(node) || xml_provider_failed(p)) return NULL;

    _cxml_node_t type = _cxml_get_node_type(node);
    if (type != CXML_ELEM_NODE && type != CXML_ROOT_NODE) return NULL;

    void *result = NULL;

    if (type == CXML_ELEM_NODE) {
        cxml_elem_node *elem = (cxml_elem_node *)node;
        /* XML names are matched exactly; no case folding is permitted. */
        if (elem->attributes) {
            cxml_for_each(at_node, &elem->attributes->keys) {
                const char *attr_key = (const char *)at_node;
                if (xml_name_eq(attr_key, expected)) {
                    cxml_attr_node *attr = cxml_table_get(elem->attributes, attr_key);
                    if (attr) return attr;
                }
            }
        }
    }

    /* Check children */
    cxml_list *children = (type == CXML_ELEM_NODE) ? &((cxml_elem_node *)node)->children : &((cxml_root_node *)node)->children;
    
    size_t count = 0;
    cxml_for_each(child, children) {
        if (_cxml_get_node_type(child) == CXML_ELEM_NODE) {
            cxml_elem_node *e = (cxml_elem_node *)child;
            const char *qname = cxml_string_as_raw(&e->name.qname);
            if (xml_name_eq(qname, expected)) {
                count++;
            }
        }
    }

    if (count == 1) {
        cxml_for_each(match_child, children) {
            if (_cxml_get_node_type(match_child) == CXML_ELEM_NODE) {
                cxml_elem_node *e = (cxml_elem_node *)match_child;
                const char *qname = cxml_string_as_raw(&e->name.qname);
                if (xml_name_eq(qname, expected)) {
                    result = e;
                    break;
                }
            }
        }
    } else if (count > 1) {
        SURROGATE_LIST *slist = add_surrogate(p, count);
        if (!slist) return NULL;
        size_t i = 0;
        cxml_for_each(match_child, children) {
            if (_cxml_get_node_type(match_child) == CXML_ELEM_NODE) {
                cxml_elem_node *e = (cxml_elem_node *)match_child;
                const char *qname = cxml_string_as_raw(&e->name.qname);
                if (xml_name_eq(qname, expected)) {
                    slist->items[i++] = e;
                }
            }
        }
        result = slist;
    }

    return result;
}

static void *xml_get_child_by_index(void *node, unsigned index, void *provider_data) {
    if (!node) return NULL;

    if (is_surrogate(node)) {
        SURROGATE_LIST *slist = (SURROGATE_LIST *)node;
        if (index < slist->count) {
            return slist->items[index];
        }
        return NULL;
    }

    /* For normal nodes, return self for index 0 (as per mustache spec for iterable scalars) */
    return (index == 0) ? node : NULL;
}

static MUSTACHE_TEMPLATE *xml_get_partial(const char *name, size_t size, void *provider_data) {
    MUSTACHE_XML_PROVIDER *provider = (MUSTACHE_XML_PROVIDER *)provider_data;
    if (!provider->template_loader) return NULL;
    return provider->template_loader(name, size, provider->user_data);
}

int mustache_render_xml(const MUSTACHE_TEMPLATE *templ, void *xml_node,
                         const MUSTACHE_RENDERER *renderer, void *renderer_data,
                         MUSTACHE_TEMPLATE *(*template_loader)(const char *, size_t,
                                                               void *),
                         void *user_data) {
    MUSTACHE_XML_PROVIDER provider;
    if (mustache_xml_provider_init(&provider, xml_node, template_loader, user_data) != 0) {
        return -1;
    }

    int rc = mustache_process(templ, renderer, renderer_data, &provider.base, &provider);
    if (mustache_xml_provider_status(&provider) != 0) rc = -1;
    mustache_xml_provider_free(&provider);
    return rc;
}
