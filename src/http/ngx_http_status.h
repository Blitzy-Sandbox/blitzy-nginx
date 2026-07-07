
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_HTTP_STATUS_H_INCLUDED_
#define _NGX_HTTP_STATUS_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


/*
 * ngx_http_status_def_t is one row of the centralized status registry (see
 * ngx_http_status.c).  reason is the wire reason-phrase literal and reason_len
 * its length; a NULL/0 pair marks a numeric-only code.  code and flags (the
 * NGX_HTTP_STATUS_* bits below) are 16-bit so the whole table stays compact.
 * ngx_http_request_t is forward-declared by ngx_http.h, which includes this
 * header, so ngx_http_request.h is intentionally not included here.
 */

typedef struct {
    const char   *reason;
    uint16_t      reason_len;
    uint16_t      code;
    uint16_t      flags;
} ngx_http_status_def_t;


/* Class/behaviour flags for ngx_http_status_def_t.flags (combinable bits). */

#define NGX_HTTP_STATUS_CACHEABLE       0x0001  /* cacheable by default (RFC 9110 15.1) */
#define NGX_HTTP_STATUS_INFORMATIONAL   0x0002  /* 1xx informational (RFC 9110 15.2) */
#define NGX_HTTP_STATUS_CLIENT_ERROR    0x0004  /* 4xx client error (RFC 9110 15.5) */
#define NGX_HTTP_STATUS_SERVER_ERROR    0x0008  /* 5xx server error (RFC 9110 15.6) */


/*
 * Centralized HTTP status registry API.  ngx_http_status_set() is the single
 * write path for r->headers_out.status and ngx_http_status_reason() is the
 * single source of the wire reason phrase.  The registry is seeded once during
 * the configuration phase, before the first worker fork, and is read-only
 * afterwards, so all worker processes share it without locking.
 */

ngx_int_t    ngx_http_status_set(ngx_http_request_t *r, ngx_uint_t status);
ngx_int_t    ngx_http_status_validate(ngx_uint_t status);
ngx_str_t    ngx_http_status_reason(ngx_uint_t status);
ngx_int_t    ngx_http_status_register(void);
ngx_uint_t   ngx_http_status_is_cacheable(ngx_uint_t status);

/*
 * ngx_http_status_log() emits a status-API log event correlated with the
 * request's $request_id.  action names the event and status is the code in
 * play; a NULL or connectionless request is ignored.
 */

void         ngx_http_status_log(ngx_http_request_t *r, ngx_uint_t level,
                 const char *action, ngx_uint_t status);


/*
 * Per-worker status-class counters surfaced by the stub_status endpoint.
 * ngx_http_status_set() bumps the class of each status it writes, and the
 * rejected slot when strict validation refuses a code.  Counters live in a
 * worker's own address space (copy-on-write after fork), so no locking is
 * required.
 */

enum {
    NGX_HTTP_STATUS_CLASS_1XX = 0,
    NGX_HTTP_STATUS_CLASS_2XX,
    NGX_HTTP_STATUS_CLASS_3XX,
    NGX_HTTP_STATUS_CLASS_4XX,
    NGX_HTTP_STATUS_CLASS_5XX,
    NGX_HTTP_STATUS_REJECTED,
    NGX_HTTP_STATUS_CLASSES
};

extern ngx_atomic_t  ngx_http_status_counters[NGX_HTTP_STATUS_CLASSES];


#endif /* _NGX_HTTP_STATUS_H_INCLUDED_ */
