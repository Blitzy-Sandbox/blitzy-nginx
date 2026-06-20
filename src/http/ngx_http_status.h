
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
 * This header is included from ngx_http.h immediately after ngx_http_request.h,
 * so ngx_http_request_t is already fully defined by the time it is processed.
 * It must not include ngx_http.h (which would be circular) nor redeclare
 * ngx_http_request_t (C99 forbids the resulting duplicate typedef).
 */


/*
 * Registry descriptor for a single HTTP status code: its numeric value, the
 * reason phrase rendered verbatim on the status line (empty for codes that
 * emit a numeric-only status line), the RFC 9110 class + cacheability flags,
 * and the RFC section that defines the code.
 */
typedef struct {
    ngx_uint_t   code;        /* e.g. 404 */
    ngx_str_t    reason;      /* full status line, e.g. "200 OK" */
    ngx_uint_t   flags;       /* class + cacheability bitmask */
    const char  *rfc_section; /* e.g. "15.5.5" */
} ngx_http_status_def_t;


/* ngx_http_status_def_t.flags bits: status class and cacheability */

#define NGX_HTTP_STATUS_CACHEABLE       0x0001
#define NGX_HTTP_STATUS_CLIENT_ERROR    0x0002
#define NGX_HTTP_STATUS_SERVER_ERROR    0x0004
#define NGX_HTTP_STATUS_INFORMATIONAL   0x0008


/* registry-backed status metadata API (implemented in ngx_http_status.c) */

ngx_int_t   ngx_http_status_validate(ngx_uint_t status);
ngx_str_t   ngx_http_status_reason(ngx_uint_t status);
ngx_int_t   ngx_http_status_register(const ngx_http_status_def_t *def);
ngx_uint_t  ngx_http_status_is_cacheable(ngx_uint_t status);

/*
 * Return the registry flag bitmask (class + cacheability) for a status code,
 * or 0 when the code is unknown to the registry.  This exposes the registry's
 * authoritative class metadata so that consumers (for example the error-page
 * renderer in ngx_http_special_response.c) can make class-based decisions from
 * the single source of truth instead of re-deriving the code-space structure.
 */
ngx_uint_t  ngx_http_status_flags(ngx_uint_t status);


/*
 * ngx_http_status_set() is the single status-assignment chokepoint.  When the
 * tree is built with --with-http_status_validation (NGX_HTTP_STATUS_VALIDATION
 * defined to 1) it is a real function that validates the code; otherwise it is
 * a zero-overhead inline identical to the legacy "r->headers_out.status = ...".
 */

#if (NGX_HTTP_STATUS_VALIDATION)

ngx_int_t   ngx_http_status_set(ngx_http_request_t *r, ngx_uint_t status);

#else

static ngx_inline ngx_int_t
ngx_http_status_set(ngx_http_request_t *r, ngx_uint_t status)
{
    r->headers_out.status = status;
    return NGX_OK;
}

#endif


/*
 * One-time, idempotent registry initialization.  Invoked from the pre-fork
 * HTTP header-filter postconfiguration hook (ngx_http_header_filter_init in
 * ngx_http_header_filter_module.c), which runs in the master process while the
 * configuration is parsed and therefore before any worker is forked; the core
 * registry is static const, so it is valid at startup and remains read-only
 * thereafter.
 */
ngx_int_t   ngx_http_status_init(void);


#endif /* _NGX_HTTP_STATUS_H_INCLUDED_ */
