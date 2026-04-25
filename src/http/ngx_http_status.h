
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_HTTP_STATUS_H_INCLUDED_
#define _NGX_HTTP_STATUS_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


/*
 * Forward declarations of opaque types.
 *
 * The full type definitions live in <ngx_http.h>, <ngx_http_request.h>, and
 * <ngx_core.h>.  This header is itself included by <ngx_http.h> after the
 * umbrella header has emitted:
 *
 *     typedef struct ngx_http_request_s     ngx_http_request_t;
 *
 * so that subsequent prototypes referencing ngx_http_request_t resolve.
 * This file does not directly reference ngx_http_request_t in its public
 * surface; the five public API prototypes (ngx_http_status_set,
 * ngx_http_status_validate, ngx_http_status_reason,
 * ngx_http_status_is_cacheable, ngx_http_status_register) are declared in
 * <ngx_http.h> after this header is pulled in.
 *
 * ngx_cycle_t is provided by <ngx_core.h>, included above, so the
 * ngx_http_status_init_registry() prototype below has the correct type
 * available without any additional include.
 */


/*
 * Status code flag bits.
 *
 * Each ngx_http_status_def_t entry's `flags` field is a bitwise OR of these
 * values.  They classify a status code along multiple orthogonal dimensions:
 *
 *   NGX_HTTP_STATUS_CACHEABLE      Heuristically cacheable per RFC 9110
 *                                  Section 15.  Used by HTTP cache modules
 *                                  to decide whether to cache a response
 *                                  when no explicit Cache-Control directive
 *                                  is present (e.g., 200, 203, 204, 206,
 *                                  300, 301, 308, 404, 405, 410, 414, 501).
 *
 *   NGX_HTTP_STATUS_CLIENT_ERROR   4xx class.  Indicates the client should
 *                                  fix their request before retrying.
 *
 *   NGX_HTTP_STATUS_SERVER_ERROR   5xx class.  Indicates a server-side
 *                                  failure; the client may safely retry.
 *
 *   NGX_HTTP_STATUS_INFORMATIONAL  1xx class.  Provisional response;
 *                                  processing continues.  A final response
 *                                  will follow.
 *
 *   NGX_HTTP_STATUS_NGINX_EXT      NGINX-specific extension (e.g., 444,
 *                                  494, 495, 496, 497, 499).  Not in the
 *                                  IANA HTTP Status Code Registry.  Useful
 *                                  for monitoring tools that audit standard
 *                                  vs. NGINX-local codes in access logs.
 *
 * Multiple flags may be set on a single entry: a 4xx code that is also
 * heuristically cacheable (e.g., 404 Not Found) carries both
 * NGX_HTTP_STATUS_CLIENT_ERROR and NGX_HTTP_STATUS_CACHEABLE.  An
 * NGINX-specific 4xx extension (e.g., 444) carries both
 * NGX_HTTP_STATUS_CLIENT_ERROR and NGX_HTTP_STATUS_NGINX_EXT.
 *
 * Mutual-exclusivity contract for the three RFC 9110 status-class flags
 * (NGX_HTTP_STATUS_INFORMATIONAL, NGX_HTTP_STATUS_CLIENT_ERROR, and
 * NGX_HTTP_STATUS_SERVER_ERROR): AT MOST ONE of these three flags may be
 * set per registry entry.  A single status code maps to exactly one RFC
 * 9110 status class; setting more than one class flag on a single entry
 * is a registry-population bug.  Note that 2xx Successful and 3xx
 * Redirection codes carry NO class flag (the absence of a class flag
 * implicitly means 2xx or 3xx; cacheability is signaled separately via
 * NGX_HTTP_STATUS_CACHEABLE).
 *
 * NGX_HTTP_STATUS_CACHEABLE and NGX_HTTP_STATUS_NGINX_EXT compose freely
 * with any class flag (or no class flag).  See the parallel discussion
 * in docs/api/status_codes.md (Type Definitions section, line 94) for
 * the same contract restated as part of the API reference.
 *
 * Bit values are powers of two and reserve the low byte for class flags;
 * the upper bits remain free for future extensions without breaking ABI.
 */
#define NGX_HTTP_STATUS_CACHEABLE       0x01
#define NGX_HTTP_STATUS_CLIENT_ERROR    0x02
#define NGX_HTTP_STATUS_SERVER_ERROR    0x04
#define NGX_HTTP_STATUS_INFORMATIONAL   0x08
#define NGX_HTTP_STATUS_NGINX_EXT       0x10


/*
 * HTTP status registry entry.
 *
 * The ngx_http_status_registry[] array in ngx_http_status.c contains one
 * entry of this type per registered status code.  The array is statically
 * initialized at compile time and is read-only after process startup, which
 * makes it inherently thread-safe across NGINX's multi-process worker model
 * (each worker has its own copy of .rodata via fork; no synchronization is
 * required).
 *
 * Field semantics:
 *
 *   code         The numeric HTTP status code.  Must fall within the RFC
 *                9110 valid range (100..599 inclusive).  NGINX-specific
 *                extensions in the 4xx range (444, 494, 495, 496, 497,
 *                499) are also valid and are flagged with
 *                NGX_HTTP_STATUS_NGINX_EXT.
 *
 *   reason       The canonical RFC 9110 reason phrase, encoded as an
 *                ngx_str_t literal (typically built via the ngx_string()
 *                macro at registry initialization).
 *
 *                NOTE: the registry's reason phrases follow RFC 9110
 *                canonical wording (e.g., 302 "Found", 408 "Request
 *                Timeout", 413 "Content Too Large"), while the wire-format
 *                ngx_http_status_lines[] table in
 *                ngx_http_header_filter_module.c retains the legacy NGINX
 *                phrasing (e.g., 302 "Moved Temporarily") for byte-level
 *                wire compatibility with existing clients and tooling.
 *                Consumers requiring the on-wire reason should consult the
 *                wire table; consumers wanting the RFC 9110 canonical
 *                wording should call ngx_http_status_reason().
 *
 *   flags        Bitwise OR of NGX_HTTP_STATUS_* flags above.  Multiple
 *                flags may be set simultaneously.  An entry with flags == 0
 *                represents a 2xx or 3xx code with no special class
 *                marking and no cacheability hint.
 *
 *   rfc_section  Static C-string reference to the defining RFC section,
 *                e.g., "RFC 9110 Section 15.5.5" for 404 Not Found.  For
 *                NGINX-specific codes the literal string "nginx extension"
 *                is used.  The pointer references read-only literal storage
 *                with static lifetime; callers MUST NOT free or mutate it.
 *
 * The struct field order is fixed and matches the user-supplied example in
 * the Agent Action Plan; do not reorder fields without coordinating with
 * the static initializer in ngx_http_status.c.
 */
typedef struct {
    ngx_uint_t    code;
    ngx_str_t     reason;
    ngx_uint_t    flags;
    const char   *rfc_section;
} ngx_http_status_def_t;


/*
 * Registry initializer (internal helper).
 *
 * The status code registry is statically populated at compile time, so this
 * initializer is effectively a no-op at runtime.  It is exported primarily
 * to provide a well-defined call site for any future cycle-scoped setup
 * (e.g., per-cycle validation hooks, debug-only consistency checks) and to
 * enforce the AAP rule "registry initialization must complete during
 * configuration parsing phase only" by giving callers an unambiguous
 * function to invoke at the end of the http{} block configuration phase.
 *
 * The current implementation in ngx_http_status.c performs only a defensive
 * NULL-pointer check on the cycle argument and returns NGX_OK on success
 * (or NGX_ERROR if cycle is NULL).  Calling this function is not required
 * for the registry to be operational; ngx_http_status_set(),
 * ngx_http_status_validate(), ngx_http_status_reason(), and
 * ngx_http_status_is_cacheable() are usable from the moment the program
 * loads, because the registry array is initialized in .rodata.
 *
 * Parameters:
 *   cycle  The active NGINX cycle (must not be NULL).
 *
 * Returns:
 *   NGX_OK     on success.
 *   NGX_ERROR  if cycle is NULL.
 */
ngx_int_t ngx_http_status_init_registry(ngx_cycle_t *cycle);


#endif /* _NGX_HTTP_STATUS_H_INCLUDED_ */
