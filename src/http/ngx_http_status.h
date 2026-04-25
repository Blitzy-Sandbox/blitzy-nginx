
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


/*
 * ngx_http_status_set: status-code assignment facade — dual-mode dispatch.
 *
 * The two build modes split the implementation strategy to satisfy AAP
 * D-004 ("the compiler will optimize the range check to a no-op in
 * permissive builds") and AAP §0.7.6 ("Confirm that gcc -O2 inlines
 * ngx_http_status_set() for compile-time-constant arguments by inspecting
 * disassembly... The expected output shows a direct store to
 * r->headers_out.status with no CALL instruction for NGX_HTTP_OK-style
 * literals").  Strict mode is opt-in (--with-http_status_validation) and
 * keeps the extensive RFC 9110 validation logic out of line in
 * ngx_http_status.c; permissive mode is the default and inlines a tiny
 * fast-path body at every call site to deliver true zero-overhead
 * dispatch.
 *
 *   Permissive mode (NGX_HTTP_STATUS_VALIDATION undefined — the default):
 *
 *     The function is defined here as `static ngx_inline`.  The body
 *     comprises only an NGX_LOG_DEBUG_HTTP trace (which the preprocessor
 *     fully eliminates in release builds — see <ngx_log.h>'s NGX_DEBUG
 *     gating of the ngx_log_debug* macros) and a single store to
 *     r->headers_out.status.  Because the body is visible to every
 *     translation unit that includes <ngx_http.h>, gcc inlines it at
 *     -O1 and higher (the default NGINX CFLAGS use -O which is
 *     equivalent to -O1, and -O1 enables inlining of small static
 *     functions visible at the call site).  For compile-time-constant
 *     status arguments (e.g., NGX_HTTP_OK), the call collapses to a
 *     single mov-immediate to r->headers_out.status, satisfying the AAP
 *     D-004 zero-overhead promise and the §0.7.6 disassembly criterion.
 *
 *     The static-inline pattern is the canonical NGINX idiom for
 *     header-resident inlinable helpers (see ngx_array_init in
 *     <ngx_array.h>, ngx_log_error in <ngx_log.h>, ngx_atomic_*
 *     primitives, and many others) and uses the same `ngx_inline`
 *     keyword, which is conditionally `inline` per <ngx_config.h>.
 *
 *   Strict mode (NGX_HTTP_STATUS_VALIDATION defined):
 *
 *     The function is declared here as a normal extern prototype; the
 *     definition lives in src/http/ngx_http_status.c and performs the
 *     full RFC 9110 strict-validation logic (range check, 1xx-after-
 *     final detection, single-final-code enforcement, upstream pass-
 *     through bypass, structured WARN/ERR logging, debug-trace).  The
 *     ~100-line body is too large to comfortably inline at every call
 *     site without bloating the binary, and strict mode is an opt-in
 *     pre-deployment validation feature where the marginal CALL/RET
 *     cost is irrelevant to the production hot path.
 *
 * Include-order requirement (permissive mode):
 *
 *   The inline body references ngx_http_request_t pointer fields
 *   (r->headers_out.status, r->upstream, r->connection->log).  The full
 *   ngx_http_request_t type lives in <ngx_http_request.h>.  This header
 *   <ngx_http_status.h> is included exclusively from <ngx_http.h> at
 *   line 35, immediately AFTER <ngx_http_request.h> is included at line
 *   34, guaranteeing that the full request type is visible whenever
 *   this inline definition is processed.  Direct inclusion of this
 *   header from a translation unit that has not first included
 *   <ngx_http_request.h> (or its umbrella <ngx_http.h>) is not a
 *   supported usage pattern.
 *
 * Parameters:
 *   r       Active request whose response status code is being set.
 *           Must not be NULL; r->connection must not be NULL;
 *           r->connection->log must not be NULL (NGINX guarantees these
 *           for any live request).
 *   status  Numeric HTTP status code to assign.
 *
 * Returns:
 *   NGX_OK     Always in permissive mode.  In strict mode: on successful
 *              validation.
 *   NGX_ERROR  Strict mode only: if validation rejects the code (out of
 *              range, 1xx-after-final, or different non-200 final-code
 *              transition — see the full transition matrix in the strict
 *              definition's comment block in ngx_http_status.c).
 *
 * Side effects:
 *   - Sets r->headers_out.status = status (always in permissive mode;
 *     conditionally in strict mode, after validation passes).
 *   - May emit a debug-level trace at NGX_LOG_DEBUG_HTTP via the
 *     canonical observability format string documented in
 *     docs/architecture/observability.md.  In strict mode validation
 *     failures additionally emit NGX_LOG_WARN or NGX_LOG_ERR entries.
 */
#if (NGX_HTTP_STATUS_VALIDATION)

ngx_int_t ngx_http_status_set(ngx_http_request_t *r, ngx_uint_t status);

#else

static ngx_inline ngx_int_t
ngx_http_status_set(ngx_http_request_t *r, ngx_uint_t status)
{
    /*
     * Emit the unified observability trace line per the canonical format
     * string contract documented in docs/architecture/observability.md
     * ("http status set: %ui %V (strict=%s upstream=%s)").  The four
     * format fields — numeric code (%ui), canonical reason phrase from
     * the registry (%V), the strict-mode flag literal (%s), and the
     * upstream-presence flag (%s) — match the strict-mode emission in
     * ngx_http_status.c so that downstream log parsers (e.g., the
     * Grafana templates in docs/architecture/observability.md) extract
     * fields uniformly across build modes.
     *
     * In release builds (NGX_DEBUG undefined) the entire ngx_log_debug4
     * macro expands to nothing — the registry-lookup call to
     * ngx_http_status_reason() and the literal-string load are both
     * fully elided by the preprocessor — leaving only the assignment
     * and return below.  In debug builds (NGX_DEBUG defined) the trace
     * is emitted and the body is somewhat larger but still inlinable.
     */
    ngx_log_debug4(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http status set: %ui %V (strict=%s upstream=%s)",
                   status, ngx_http_status_reason(status),
                   "no",
                   r->upstream != NULL ? "yes" : "no");

    /*
     * The single hot-path operation: store the numeric status code into
     * r->headers_out.status.  For compile-time-constant status arguments
     * the compiler folds this to a mov-immediate at the call site;
     * for runtime-variable status arguments the compiler emits a single
     * mov from the source register.  Either way, no CALL instruction is
     * generated for ngx_http_status_set itself in optimized permissive
     * builds — the AAP D-004 design contract is realized.
     */
    r->headers_out.status = status;

    return NGX_OK;
}

#endif


#endif /* _NGX_HTTP_STATUS_H_INCLUDED_ */
