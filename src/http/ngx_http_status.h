
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */

/*
 * ngx_http_status.h - single source of truth for HTTP status-code metadata.
 *
 * Centralizes the identity of every HTTP status code (numeric value, reason
 * phrase, RFC 9110 class + cacheability flags, and RFC section reference) and
 * exposes the API that mediates status assignment and lookup.  Replaces the
 * previously duplicated reason-phrase / error-page tables and the divergent
 * per-file offset macros.
 */


#ifndef _NGX_HTTP_STATUS_H_INCLUDED_
#define _NGX_HTTP_STATUS_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


/*
 * Descriptor for a single HTTP status code: its numeric value, the reason
 * phrase, the RFC 9110 class / cacheability flags, and the RFC section that
 * defines it.  The "reason" field holds the complete status-line text exactly
 * as it appears on the wire, including the numeric prefix (e.g. "404 Not
 * Found"); codes that are emitted numeric-only carry an empty ngx_str_t
 * (ngx_null_string), preserving byte-for-byte wire output.
 */

typedef struct {
    ngx_uint_t   code;        /* e.g. 404 */
    ngx_str_t    reason;      /* e.g. "404 Not Found" */
    ngx_uint_t   flags;       /* class + cacheability bitmask */
    const char  *rfc_section; /* e.g. "15.5.5" */
} ngx_http_status_def_t;


/* status-code metadata flags: RFC 9110 class and cacheability */

#define NGX_HTTP_STATUS_CACHEABLE       0x0001
#define NGX_HTTP_STATUS_CLIENT_ERROR    0x0002
#define NGX_HTTP_STATUS_SERVER_ERROR    0x0004
#define NGX_HTTP_STATUS_INFORMATIONAL   0x0008


/* validate a status code against RFC 9110 Section 15; NGX_OK if conformant */
ngx_int_t   ngx_http_status_validate(ngx_uint_t status);

/* full status-line text, or an empty string for numeric-only codes */
ngx_str_t   ngx_http_status_reason(ngx_uint_t status);

/* register an additional status definition (extensibility seam, pre-fork) */
ngx_int_t   ngx_http_status_register(const ngx_http_status_def_t *def);

/* non-zero if the code carries the cacheable metadata flag */
ngx_uint_t  ngx_http_status_is_cacheable(ngx_uint_t status);


/*
 * ngx_http_status_set() is the single chokepoint for assigning a response
 * status.  Its shape depends on the build-time validation flag.
 */

#if (NGX_HTTP_STATUS_VALIDATION)

/*
 * strict-validation build: the code is checked against the registry before it
 * is stored (proxied responses bypass the check to preserve pass-through);
 * implemented in ngx_http_status.c.
 */
ngx_int_t   ngx_http_status_set(ngx_http_request_t *r, ngx_uint_t status);

#else

/*
 * default build: zero-overhead inline that compiles to the original field
 * write; constant status arguments (e.g. NGX_HTTP_OK -> 200) constant-fold, so
 * the generated code is byte-for-byte identical to the legacy assignment.
 */
static ngx_inline ngx_int_t
ngx_http_status_set(ngx_http_request_t *r, ngx_uint_t status)
{
    r->headers_out.status = status;
    return NGX_OK;
}

#endif


/*
 * One-time, idempotent initialization of the status registry.  Called before
 * the worker fork; the core registry is static const and therefore already
 * valid and read-only once this returns.
 */
ngx_int_t   ngx_http_status_init(void);


#endif /* _NGX_HTTP_STATUS_H_INCLUDED_ */
