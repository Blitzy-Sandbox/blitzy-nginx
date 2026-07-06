
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_HTTP_STATUS_H_INCLUDED_
#define _NGX_HTTP_STATUS_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


/*
 * ngx_http_status_def_t describes a single HTTP status code in the centralized
 * status registry (see ngx_http_status.c).  The member order and types are
 * fixed: the numeric code, the wire reason phrase, the class/behavior flags
 * defined below, and a static RFC 9110 section reference used for diagnostics.
 * ngx_http_request_t is forward-declared by ngx_http.h, which includes this
 * header, so ngx_http_request.h is intentionally not included here.
 */

typedef struct {
    ngx_uint_t    code;
    ngx_str_t     reason;
    ngx_uint_t    flags;
    const char   *rfc_section;
} ngx_http_status_def_t;


/*
 * Class/behavior flags for ngx_http_status_def_t.flags.  Each flag is a
 * distinct power of two so that several may be combined for a single code.
 * The set mirrors the RFC 9110 section 15 status classes seeded by the
 * registry; every flag defined here is assigned in ngx_http_status.c.
 */

#define NGX_HTTP_STATUS_CACHEABLE       0x0001  /* cacheable by default (RFC 9110 15.1) */
#define NGX_HTTP_STATUS_INFORMATIONAL   0x0002  /* 1xx informational (RFC 9110 15.2) */
#define NGX_HTTP_STATUS_CLIENT_ERROR    0x0004  /* 4xx client error (RFC 9110 15.5) */
#define NGX_HTTP_STATUS_SERVER_ERROR    0x0008  /* 5xx server error (RFC 9110 15.6) */


/*
 * Centralized HTTP status registry API.  ngx_http_status_set() is the single
 * write path for r->headers_out.status and ngx_http_status_reason() is the
 * single source of the wire reason phrase; the header filter and error-page
 * handler delegate to them.  The registry is seeded once during the
 * configuration phase, before the first worker fork, and is read-only
 * afterwards, so all worker processes share it without locking.  No registry
 * array or runtime-mutation function is exposed by this header.
 */

ngx_int_t    ngx_http_status_set(ngx_http_request_t *r, ngx_uint_t status);
ngx_int_t    ngx_http_status_validate(ngx_uint_t status);
ngx_str_t    ngx_http_status_reason(ngx_uint_t status);
ngx_int_t    ngx_http_status_register(void);
ngx_uint_t   ngx_http_status_is_cacheable(ngx_uint_t status);


#endif /* _NGX_HTTP_STATUS_H_INCLUDED_ */
