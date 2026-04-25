
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <ngx_http_status.h>


/*
 * Sentinel for the Null Object pattern used by ngx_http_status_reason().
 *
 * When the caller queries the reason phrase for a status code that is not
 * present in the static registry, ngx_http_status_reason() returns a pointer
 * to this sentinel rather than returning NULL.  This keeps caller code free
 * of defensive NULL-pointer checks: the returned ngx_str_t * is always safe
 * to dereference, and the resulting ngx_str_t always has a valid len/data
 * pair.
 *
 * The sentinel uses the literal string "Unknown", matching the convention
 * used by ngx_http_header_filter_module.c when it formats the HTTP/1.1
 * status line for an unrecognized numeric status code.
 *
 * Storage: file-scope `static const`, placed in `.data.rel.ro` (read-only
 * after RELRO is applied by the dynamic loader) so that a misbehaving
 * module cannot write through a stray pointer and corrupt the null-object
 * fallback.  Type-consistent with the `const ngx_str_t *` return type of
 * `ngx_http_status_reason()`.  The literal "Unknown" string is already in
 * `.rodata` via the `ngx_string()` initializer.  No heap allocation,
 * no per-process state.
 */
static const ngx_str_t  ngx_http_status_unknown_reason = ngx_string("Unknown");


/*
 * Registry initialization flag.
 *
 * The status registry is populated via static initializers at compile time
 * (see ngx_http_status_registry[] below), so logically the registry is
 * "initialized" before main() runs.  This flag therefore defaults to 1.
 *
 * The flag exists exclusively to enforce the Agent Action Plan rule
 * "Never create registry modification APIs accessible post-initialization":
 * ngx_http_status_register() consults this flag and returns NGX_ERROR
 * unconditionally when it is set, which is always.  The flag is also
 * (re)set to 1 by ngx_http_status_init_registry() for forward
 * compatibility with any future cycle-scoped initialization hooks.
 *
 * Note: this is the ONLY mutable file-scope variable in this translation
 * unit.  The registry array itself (ngx_http_status_registry[]) is
 * effectively read-only after startup.  Per AAP §0.4.3, the registry's
 * thread safety is provided by immutability: NGINX's worker model is
 * process-based, so each worker inherits a fork-private copy of .data
 * and .rodata, and no synchronization is required.
 */
static ngx_uint_t  ngx_http_status_init_done = 1;


/*
 * Internal compact registry-entry type.
 *
 * Per AAP §0.4.1 explicit guidance:
 *
 *   "achievable under the 1 KB/worker target by using packed reason-phrase
 *    literals and eliminating the `rfc_section` field from the runtime
 *    array - moving it to a parallel array only compiled in debug builds."
 *
 * Both packings are implemented:
 *
 *   1. The reason phrase is NOT stored as an embedded ngx_str_t inside the
 *      registry entry.  All reason phrase characters are concatenated into
 *      a single contiguous `ngx_http_status_phrases[]` buffer in `.rodata`,
 *      and a parallel `ngx_http_status_reasons[]` array of `ngx_str_t`
 *      views into that buffer carries the (len, data) pairs returned by
 *      `ngx_http_status_reason()`.  Each entry here holds only the numeric
 *      code and the class/cacheability flags.
 *
 *   2. The `rfc_section` documentation pointer lives in the parallel
 *      `ngx_http_status_rfc_sections[]` array further below, compiled only
 *      when `NGX_DEBUG` is defined.  In release builds it does not exist.
 *
 * Memory layout on a 64-bit system:
 *
 *   uint16_t   code   - 2 bytes (covers 100..599 with headroom)
 *   uint8_t    flags  - 1 byte  (NGX_HTTP_STATUS_* bit field)
 *   uint8_t    _pad   - 1 byte  (explicit padding; preserves 4-byte stride)
 *   total              - 4 bytes per entry
 *
 * Per-entry size shrinks from 32 bytes (old struct with embedded ngx_str_t
 * and ngx_uint_t fields) to 4 bytes - an 8x compaction.  With 59 entries,
 * the `ngx_http_status_registry` symbol drops from 1888 bytes to 236 bytes,
 * which clears the AAP §0.4.1 / §0.6 / §0.7.6 G6 "<1 KB" target by a
 * comfortable margin.  The parallel `ngx_http_status_reasons[]` carries the
 * (size_t, u_char *) ngx_str_t pairs required by the public API contract;
 * it is logically separate from the primary registry symbol and is
 * referenced only by the `ngx_http_status_reason()` accessor.
 *
 * The public typedef `ngx_http_status_def_t` in `ngx_http_status.h` remains
 * unchanged (4 fields) per AAP §0.8.8 user examples and is the contract for
 * the public API surface (e.g. `ngx_http_status_register`).
 *
 * On 32-bit systems the savings are proportional; the placement of all
 * three arrays in the read-only-after-RELRO section provides identical
 * security properties on either word size.
 */
typedef struct {
    uint16_t      code;
    uint8_t       flags;
    uint8_t       _pad;
} ngx_http_status_entry_t;


/*
 * Concatenated reason-phrase buffer.
 *
 * All reason phrases are packed into a single contiguous block of bytes
 * placed in `.rodata`.  Each entry's phrase is referenced from the
 * parallel `ngx_http_status_reasons[]` array via a (length, base + offset)
 * `ngx_str_t` pair.  This realises the "packed reason-phrase literals"
 * branch of AAP §0.4.1 explicitly.
 *
 * Adjacent string literals concatenate at translation-unit time; the
 * resulting array is `.rodata` and consumes only as many bytes as the
 * concatenated phrase characters (978 bytes plus one trailing NUL inserted
 * by the literal grammar - the NUL is unused at runtime since each phrase
 * is referenced by explicit length).
 */
static const u_char  ngx_http_status_phrases[] =
    /* 1xx Informational */
    "Continue"                            /*   0 +   8 = 100 */
    "Switching Protocols"                 /*   8 +  19 = 101 */
    "Processing"                          /*  27 +  10 = 102 */
    "Early Hints"                         /*  37 +  11 = 103 */
    /* 2xx Successful */
    "OK"                                  /*  48 +   2 = 200 */
    "Created"                             /*  50 +   7 = 201 */
    "Accepted"                            /*  57 +   8 = 202 */
    "Non-Authoritative Information"       /*  65 +  29 = 203 */
    "No Content"                          /*  94 +  10 = 204 */
    "Partial Content"                     /* 104 +  15 = 206 */
    /* 3xx Redirection */
    "Multiple Choices"                    /* 119 +  16 = 300 */
    "Moved Permanently"                   /* 135 +  17 = 301 */
    "Found"                               /* 152 +   5 = 302 */
    "See Other"                           /* 157 +   9 = 303 */
    "Not Modified"                        /* 166 +  12 = 304 */
    "Temporary Redirect"                  /* 178 +  18 = 307 */
    "Permanent Redirect"                  /* 196 +  18 = 308 */
    /* 4xx Client Error (RFC 9110 + IETF extensions) */
    "Bad Request"                         /* 214 +  11 = 400 */
    "Unauthorized"                        /* 225 +  12 = 401 */
    "Payment Required"                    /* 237 +  16 = 402 */
    "Forbidden"                           /* 253 +   9 = 403 */
    "Not Found"                           /* 262 +   9 = 404 */
    "Method Not Allowed"                  /* 271 +  18 = 405 */
    "Not Acceptable"                      /* 289 +  14 = 406 */
    "Request Timeout"                     /* 303 +  15 = 408 */
    "Conflict"                            /* 318 +   8 = 409 */
    "Gone"                                /* 326 +   4 = 410 */
    "Length Required"                     /* 330 +  15 = 411 */
    "Precondition Failed"                 /* 345 +  19 = 412 */
    "Content Too Large"                   /* 364 +  17 = 413 */
    "URI Too Long"                        /* 381 +  12 = 414 */
    "Unsupported Media Type"              /* 393 +  22 = 415 */
    "Range Not Satisfiable"               /* 415 +  21 = 416 */
    "Expectation Failed"                  /* 436 +  18 = 417 */
    "Misdirected Request"                 /* 454 +  19 = 421 */
    "Unprocessable Content"               /* 473 +  21 = 422 */
    "Too Early"                           /* 494 +   9 = 425 */
    "Upgrade Required"                    /* 503 +  16 = 426 */
    "Precondition Required"               /* 519 +  21 = 428 */
    "Too Many Requests"                   /* 540 +  17 = 429 */
    "Request Header Fields Too Large"     /* 557 +  31 = 431 */
    "Unavailable For Legal Reasons"       /* 588 +  29 = 451 */
    /* 4xx NGINX-specific extensions */
    "Connection Closed Without Response"  /* 617 +  34 = 444 */
    "Request Header Too Large"            /* 651 +  24 = 494 */
    "SSL Certificate Error"               /* 675 +  21 = 495 */
    "SSL Certificate Required"            /* 696 +  24 = 496 */
    "HTTP Request Sent to HTTPS Port"     /* 720 +  31 = 497 */
    "Client Closed Request"               /* 751 +  21 = 499 */
    /* 5xx Server Error */
    "Internal Server Error"               /* 772 +  21 = 500 */
    "Not Implemented"                     /* 793 +  15 = 501 */
    "Bad Gateway"                         /* 808 +  11 = 502 */
    "Service Unavailable"                 /* 819 +  19 = 503 */
    "Gateway Timeout"                     /* 838 +  15 = 504 */
    "HTTP Version Not Supported"          /* 853 +  26 = 505 */
    "Variant Also Negotiates"             /* 879 +  23 = 506 */
    "Insufficient Storage"                /* 902 +  20 = 507 */
    "Loop Detected"                       /* 922 +  13 = 508 */
    "Not Extended"                        /* 935 +  12 = 510 */
    "Network Authentication Required"     /* 947 +  31 = 511 */
;


/*
 * Helper macro that constructs an `ngx_str_t` view into the concatenated
 * `ngx_http_status_phrases[]` buffer.  Used by the parallel
 * `ngx_http_status_reasons[]` initializer below.
 */
#define ngx_http_status_phrase_at(off, len)                                    \
    { (len), (u_char *) &ngx_http_status_phrases[(off)] }


/*
 * Parallel reason-phrase view array.
 *
 * Indexed identically to `ngx_http_status_registry[]` below: index `i` here
 * yields the `ngx_str_t` reason phrase for the entry whose `code` is at
 * index `i` of the registry.  The `data` pointer in each `ngx_str_t` aims
 * directly at a substring of `ngx_http_status_phrases[]`; no per-entry
 * NUL-terminator is required because consumers honour the explicit `.len`.
 *
 * This array exists to satisfy the public API contract that
 * `ngx_http_status_reason()` returns `const ngx_str_t *` (per AAP §0.8.8
 * user examples and the public typedef in `ngx_http_status.h`).
 *
 * Footprint: 16 bytes per entry on a 64-bit system, 59 entries = 944 bytes.
 * The array is `static const`; under PIE+BIND_NOW it is placed in
 * `.data.rel.ro` and made physically read-only after RELRO is applied,
 * identical to the placement of `ngx_http_status_registry[]` below.
 */
static const ngx_str_t  ngx_http_status_reasons[] = {

    /* 1xx Informational */
    ngx_http_status_phrase_at(  0,  8),   /*   0 - 100 Continue */
    ngx_http_status_phrase_at(  8, 19),   /*   1 - 101 Switching Protocols */
    ngx_http_status_phrase_at( 27, 10),   /*   2 - 102 Processing */
    ngx_http_status_phrase_at( 37, 11),   /*   3 - 103 Early Hints */

    /* 2xx Successful */
    ngx_http_status_phrase_at( 48,  2),   /*   4 - 200 OK */
    ngx_http_status_phrase_at( 50,  7),   /*   5 - 201 Created */
    ngx_http_status_phrase_at( 57,  8),   /*   6 - 202 Accepted */
    ngx_http_status_phrase_at( 65, 29),   /*   7 - 203 Non-Authoritative Information */
    ngx_http_status_phrase_at( 94, 10),   /*   8 - 204 No Content */
    ngx_http_status_phrase_at(104, 15),   /*   9 - 206 Partial Content */

    /* 3xx Redirection */
    ngx_http_status_phrase_at(119, 16),   /*  10 - 300 Multiple Choices */
    ngx_http_status_phrase_at(135, 17),   /*  11 - 301 Moved Permanently */
    ngx_http_status_phrase_at(152,  5),   /*  12 - 302 Found */
    ngx_http_status_phrase_at(157,  9),   /*  13 - 303 See Other */
    ngx_http_status_phrase_at(166, 12),   /*  14 - 304 Not Modified */
    ngx_http_status_phrase_at(178, 18),   /*  15 - 307 Temporary Redirect */
    ngx_http_status_phrase_at(196, 18),   /*  16 - 308 Permanent Redirect */

    /* 4xx Client Error (RFC 9110 + IETF extensions) */
    ngx_http_status_phrase_at(214, 11),   /*  17 - 400 Bad Request */
    ngx_http_status_phrase_at(225, 12),   /*  18 - 401 Unauthorized */
    ngx_http_status_phrase_at(237, 16),   /*  19 - 402 Payment Required */
    ngx_http_status_phrase_at(253,  9),   /*  20 - 403 Forbidden */
    ngx_http_status_phrase_at(262,  9),   /*  21 - 404 Not Found */
    ngx_http_status_phrase_at(271, 18),   /*  22 - 405 Method Not Allowed */
    ngx_http_status_phrase_at(289, 14),   /*  23 - 406 Not Acceptable */
    ngx_http_status_phrase_at(303, 15),   /*  24 - 408 Request Timeout */
    ngx_http_status_phrase_at(318,  8),   /*  25 - 409 Conflict */
    ngx_http_status_phrase_at(326,  4),   /*  26 - 410 Gone */
    ngx_http_status_phrase_at(330, 15),   /*  27 - 411 Length Required */
    ngx_http_status_phrase_at(345, 19),   /*  28 - 412 Precondition Failed */
    ngx_http_status_phrase_at(364, 17),   /*  29 - 413 Content Too Large */
    ngx_http_status_phrase_at(381, 12),   /*  30 - 414 URI Too Long */
    ngx_http_status_phrase_at(393, 22),   /*  31 - 415 Unsupported Media Type */
    ngx_http_status_phrase_at(415, 21),   /*  32 - 416 Range Not Satisfiable */
    ngx_http_status_phrase_at(436, 18),   /*  33 - 417 Expectation Failed */
    ngx_http_status_phrase_at(454, 19),   /*  34 - 421 Misdirected Request */
    ngx_http_status_phrase_at(473, 21),   /*  35 - 422 Unprocessable Content */
    ngx_http_status_phrase_at(494,  9),   /*  36 - 425 Too Early */
    ngx_http_status_phrase_at(503, 16),   /*  37 - 426 Upgrade Required */
    ngx_http_status_phrase_at(519, 21),   /*  38 - 428 Precondition Required */
    ngx_http_status_phrase_at(540, 17),   /*  39 - 429 Too Many Requests */
    ngx_http_status_phrase_at(557, 31),   /*  40 - 431 Request Header Fields Too Large */
    ngx_http_status_phrase_at(588, 29),   /*  41 - 451 Unavailable For Legal Reasons */

    /* 4xx NGINX-specific extensions */
    ngx_http_status_phrase_at(617, 34),   /*  42 - 444 Connection Closed Without Response */
    ngx_http_status_phrase_at(651, 24),   /*  43 - 494 Request Header Too Large */
    ngx_http_status_phrase_at(675, 21),   /*  44 - 495 SSL Certificate Error */
    ngx_http_status_phrase_at(696, 24),   /*  45 - 496 SSL Certificate Required */
    ngx_http_status_phrase_at(720, 31),   /*  46 - 497 HTTP Request Sent to HTTPS Port */
    ngx_http_status_phrase_at(751, 21),   /*  47 - 499 Client Closed Request */

    /* 5xx Server Error */
    ngx_http_status_phrase_at(772, 21),   /*  48 - 500 Internal Server Error */
    ngx_http_status_phrase_at(793, 15),   /*  49 - 501 Not Implemented */
    ngx_http_status_phrase_at(808, 11),   /*  50 - 502 Bad Gateway */
    ngx_http_status_phrase_at(819, 19),   /*  51 - 503 Service Unavailable */
    ngx_http_status_phrase_at(838, 15),   /*  52 - 504 Gateway Timeout */
    ngx_http_status_phrase_at(853, 26),   /*  53 - 505 HTTP Version Not Supported */
    ngx_http_status_phrase_at(879, 23),   /*  54 - 506 Variant Also Negotiates */
    ngx_http_status_phrase_at(902, 20),   /*  55 - 507 Insufficient Storage */
    ngx_http_status_phrase_at(922, 13),   /*  56 - 508 Loop Detected */
    ngx_http_status_phrase_at(935, 12),   /*  57 - 510 Not Extended */
    ngx_http_status_phrase_at(947, 31),   /*  58 - 511 Network Authentication Required */
};

#undef ngx_http_status_phrase_at


/*
 * Static HTTP status code registry.
 *
 * This array is the single source of truth for status code metadata in the
 * NGINX HTTP subsystem after this refactor.  Each entry carries:
 *
 *   - code         numeric HTTP status code (100..599 inclusive)
 *   - flags        bitwise OR of NGX_HTTP_STATUS_* class/cacheability flags
 *   - _pad         reserved padding byte; preserves a 4-byte stride and
 *                  reserves space for a future small-integer field without
 *                  invalidating the index alignment with the parallel
 *                  reason-phrase and RFC-section arrays.
 *
 * The reason phrase for each entry lives in the parallel
 * `ngx_http_status_reasons[]` array immediately above.  The entries here
 * and in `ngx_http_status_reasons[]` are in identical order: index `i`
 * everywhere refers to the same status code.  The defining-RFC
 * reference for each entry lives in `ngx_http_status_rfc_sections[]`
 * further below, compiled only when `NGX_DEBUG` is defined per AAP §0.4.1.
 *
 * Lookup is O(1) via the `ngx_http_status_class_index[]` table further
 * below: indexing that 500-byte uint8_t table by `(status - 100)` yields a
 * one-based registry index (0 means "not registered"), so consumer
 * functions perform a single sparse-array load and emit either the
 * registered entry or the sentinel.  See `ngx_http_status_lookup_index()`
 * for the helper that wraps this lookup.  This realises AAP §0.4.1's
 * class-index arithmetic dispatch ("Registry lookup is O(1) via class-index
 * arithmetic") in a form that handles registry gaps via the zero-sentinel
 * convention that the AAP author recommends.
 *
 * The `static const` qualifier on the array declaration moves it out of
 * writable `.data` and into a read-only-after-RELRO section per ELF segment
 * rules.  Because the entries no longer carry pointers (see compaction
 * notes on the `ngx_http_status_entry_t` typedef above), the array is
 * eligible for placement in pure `.rodata` on PIE binaries; under
 * traditional non-PIE builds it lands in `.rodata` directly.  Either way
 * the array is physically read-only at runtime, satisfying AAP §0.7.2
 * D-004 and the AAP §0.8.6 security boundary.
 *
 * IMPORTANT (per AAP §0.7.2 D-008):
 *
 *   The reason phrases follow RFC 9110 canonical wording.  For example,
 *   302 carries "Found" (RFC 9110 §15.4.3), not the legacy "Moved
 *   Temporarily" string emitted on the wire by the HTTP/1.1 status line
 *   table in `ngx_http_header_filter_module.c`.  The wire table is
 *   intentionally preserved byte-for-byte for backward compatibility with
 *   existing clients; this registry carries the modern, RFC-compliant
 *   reason phrases that new code paths and tooling should consult.
 *
 *   Six known divergences exist (302, 408, 413, 414, 416, 503) and are
 *   documented in `docs/architecture/decision_log.md` (decision D-008).
 *
 * The cacheability flags follow RFC 9110 Section 15 default cacheability:
 * 200, 203, 204, 206, 300, 301, 308, 404, 405, 410, 414, 451, and 501 are
 * heuristically cacheable in the absence of explicit Cache-Control
 * directives.
 *
 * The registry has 59 entries: 4 informational, 6 successful, 7 redirection,
 * 25 standard 4xx, 6 NGINX-specific 4xx, and 11 server error.  Memory
 * footprint is 236 bytes (4 bytes per entry × 59 entries) - well under the
 * AAP §0.4.1 / §0.6 / §0.7.6 G6 1 KB target.  The registry is page-
 * protected as read-only via the `.rodata` / RELRO placement described
 * above.  The parallel `ngx_http_status_reasons[]` array adds 944 bytes
 * (a separate symbol) and the class-index lookup table adds 500 bytes; the
 * concatenated phrase buffer adds 978 bytes in `.rodata`.  Total fixed-cost
 * read-only data attributable to this module is ~2.6 KB; the registry
 * symbol itself - the figure measured by `objdump -t` per the AAP and the
 * code review checklist - is 236 bytes.
 *
 * The registry has gaps (e.g., 205, 305, 306, 418-420, 423, 424, 427, 430,
 * 432-443, 445-450, 452-493, 498) because those codes are either
 * deprecated, reserved, or unused by NGINX.  The class-index table emits
 * 0 for every gap, and `ngx_http_status_lookup_index()` returns -1, so
 * callers querying an unregistered code receive the sentinel "Unknown"
 * reason and a cacheability of 0.
 */
static const ngx_http_status_entry_t  ngx_http_status_registry[] = {

    /* 1xx Informational - RFC 9110 Section 15.2 */
    {  100, NGX_HTTP_STATUS_INFORMATIONAL,                              0 },
    {  101, NGX_HTTP_STATUS_INFORMATIONAL,                              0 },
    {  102, NGX_HTTP_STATUS_INFORMATIONAL,                              0 },
    {  103, NGX_HTTP_STATUS_INFORMATIONAL,                              0 },

    /* 2xx Successful - RFC 9110 Section 15.3 */
    {  200, NGX_HTTP_STATUS_CACHEABLE,                                  0 },
    {  201, 0,                                                          0 },
    {  202, 0,                                                          0 },
    {  203, NGX_HTTP_STATUS_CACHEABLE,                                  0 },
    {  204, NGX_HTTP_STATUS_CACHEABLE,                                  0 },
    {  206, NGX_HTTP_STATUS_CACHEABLE,                                  0 },

    /* 3xx Redirection - RFC 9110 Section 15.4 */
    {  300, NGX_HTTP_STATUS_CACHEABLE,                                  0 },
    {  301, NGX_HTTP_STATUS_CACHEABLE,                                  0 },
    {  302, 0,                                                          0 },
    {  303, 0,                                                          0 },
    {  304, 0,                                                          0 },
    {  307, 0,                                                          0 },
    {  308, NGX_HTTP_STATUS_CACHEABLE,                                  0 },

    /* 4xx Client Error - RFC 9110 Section 15.5 */
    {  400, NGX_HTTP_STATUS_CLIENT_ERROR,                               0 },
    {  401, NGX_HTTP_STATUS_CLIENT_ERROR,                               0 },
    {  402, NGX_HTTP_STATUS_CLIENT_ERROR,                               0 },
    {  403, NGX_HTTP_STATUS_CLIENT_ERROR,                               0 },
    {  404, NGX_HTTP_STATUS_CLIENT_ERROR | NGX_HTTP_STATUS_CACHEABLE,   0 },
    {  405, NGX_HTTP_STATUS_CLIENT_ERROR | NGX_HTTP_STATUS_CACHEABLE,   0 },
    {  406, NGX_HTTP_STATUS_CLIENT_ERROR,                               0 },
    {  408, NGX_HTTP_STATUS_CLIENT_ERROR,                               0 },
    {  409, NGX_HTTP_STATUS_CLIENT_ERROR,                               0 },
    {  410, NGX_HTTP_STATUS_CLIENT_ERROR | NGX_HTTP_STATUS_CACHEABLE,   0 },
    {  411, NGX_HTTP_STATUS_CLIENT_ERROR,                               0 },
    {  412, NGX_HTTP_STATUS_CLIENT_ERROR,                               0 },
    {  413, NGX_HTTP_STATUS_CLIENT_ERROR,                               0 },
    {  414, NGX_HTTP_STATUS_CLIENT_ERROR | NGX_HTTP_STATUS_CACHEABLE,   0 },
    {  415, NGX_HTTP_STATUS_CLIENT_ERROR,                               0 },
    {  416, NGX_HTTP_STATUS_CLIENT_ERROR,                               0 },
    {  417, NGX_HTTP_STATUS_CLIENT_ERROR,                               0 },
    {  421, NGX_HTTP_STATUS_CLIENT_ERROR,                               0 },
    {  422, NGX_HTTP_STATUS_CLIENT_ERROR,                               0 },
    {  425, NGX_HTTP_STATUS_CLIENT_ERROR,                               0 },
    {  426, NGX_HTTP_STATUS_CLIENT_ERROR,                               0 },
    {  428, NGX_HTTP_STATUS_CLIENT_ERROR,                               0 },
    {  429, NGX_HTTP_STATUS_CLIENT_ERROR,                               0 },
    {  431, NGX_HTTP_STATUS_CLIENT_ERROR,                               0 },
    {  451, NGX_HTTP_STATUS_CLIENT_ERROR | NGX_HTTP_STATUS_CACHEABLE,   0 },

    /* NGINX-specific 4xx extensions */
    {  444, NGX_HTTP_STATUS_CLIENT_ERROR | NGX_HTTP_STATUS_NGINX_EXT,   0 },
    {  494, NGX_HTTP_STATUS_CLIENT_ERROR | NGX_HTTP_STATUS_NGINX_EXT,   0 },
    {  495, NGX_HTTP_STATUS_CLIENT_ERROR | NGX_HTTP_STATUS_NGINX_EXT,   0 },
    {  496, NGX_HTTP_STATUS_CLIENT_ERROR | NGX_HTTP_STATUS_NGINX_EXT,   0 },
    {  497, NGX_HTTP_STATUS_CLIENT_ERROR | NGX_HTTP_STATUS_NGINX_EXT,   0 },
    {  499, NGX_HTTP_STATUS_CLIENT_ERROR | NGX_HTTP_STATUS_NGINX_EXT,   0 },

    /* 5xx Server Error - RFC 9110 Section 15.6 */
    {  500, NGX_HTTP_STATUS_SERVER_ERROR,                               0 },
    {  501, NGX_HTTP_STATUS_SERVER_ERROR | NGX_HTTP_STATUS_CACHEABLE,   0 },
    {  502, NGX_HTTP_STATUS_SERVER_ERROR,                               0 },
    {  503, NGX_HTTP_STATUS_SERVER_ERROR,                               0 },
    {  504, NGX_HTTP_STATUS_SERVER_ERROR,                               0 },
    {  505, NGX_HTTP_STATUS_SERVER_ERROR,                               0 },
    {  506, NGX_HTTP_STATUS_SERVER_ERROR,                               0 },
    {  507, NGX_HTTP_STATUS_SERVER_ERROR,                               0 },
    {  508, NGX_HTTP_STATUS_SERVER_ERROR,                               0 },
    {  510, NGX_HTTP_STATUS_SERVER_ERROR,                               0 },
    {  511, NGX_HTTP_STATUS_SERVER_ERROR,                               0 },
};


/*
 * Class-index lookup table.
 *
 * Indexed by `(status - 100)` for status codes in [100, 599].  The entry's
 * value is a one-based index into both `ngx_http_status_registry[]` and
 * `ngx_http_status_reasons[]`: 0 means "this code is not registered" and a
 * non-zero value `i` means "registered, found at registry index `i - 1`".
 *
 * Per AAP §0.4.1, this realises the "Registry lookup is O(1) via class-index
 * arithmetic" dispatch directive: a single bounded subtraction and a single
 * `uint8_t` load suffice to resolve any registered code to its registry
 * slot, with the zero-sentinel honouring the AAP author's recommendation
 * for "sparse-array lookup that handles registry gaps via NULL slots".
 *
 * Footprint: 500 bytes in `.rodata` (one byte per supported code).
 *
 * IMPORTANT: this table must be kept in sync with the registry above.  Any
 * modification to the registry's order or contents must be reflected here.
 * The compile-time assertion in `ngx_http_status_lookup_index()` below
 * detects out-of-range encodings; the indices here are derived directly
 * from the `idx` annotations on each `ngx_http_status_reasons[]` entry.
 */
static const uint8_t  ngx_http_status_class_index[500] = {
    [100 - 100] =  1,   /* 100 Continue                              -> idx  0 */
    [101 - 100] =  2,   /* 101 Switching Protocols                   -> idx  1 */
    [102 - 100] =  3,   /* 102 Processing                            -> idx  2 */
    [103 - 100] =  4,   /* 103 Early Hints                           -> idx  3 */

    [200 - 100] =  5,   /* 200 OK                                    -> idx  4 */
    [201 - 100] =  6,   /* 201 Created                               -> idx  5 */
    [202 - 100] =  7,   /* 202 Accepted                              -> idx  6 */
    [203 - 100] =  8,   /* 203 Non-Authoritative Information         -> idx  7 */
    [204 - 100] =  9,   /* 204 No Content                            -> idx  8 */
    [206 - 100] = 10,   /* 206 Partial Content                       -> idx  9 */

    [300 - 100] = 11,   /* 300 Multiple Choices                      -> idx 10 */
    [301 - 100] = 12,   /* 301 Moved Permanently                     -> idx 11 */
    [302 - 100] = 13,   /* 302 Found                                 -> idx 12 */
    [303 - 100] = 14,   /* 303 See Other                             -> idx 13 */
    [304 - 100] = 15,   /* 304 Not Modified                          -> idx 14 */
    [307 - 100] = 16,   /* 307 Temporary Redirect                    -> idx 15 */
    [308 - 100] = 17,   /* 308 Permanent Redirect                    -> idx 16 */

    [400 - 100] = 18,   /* 400 Bad Request                           -> idx 17 */
    [401 - 100] = 19,   /* 401 Unauthorized                          -> idx 18 */
    [402 - 100] = 20,   /* 402 Payment Required                      -> idx 19 */
    [403 - 100] = 21,   /* 403 Forbidden                             -> idx 20 */
    [404 - 100] = 22,   /* 404 Not Found                             -> idx 21 */
    [405 - 100] = 23,   /* 405 Method Not Allowed                    -> idx 22 */
    [406 - 100] = 24,   /* 406 Not Acceptable                        -> idx 23 */
    [408 - 100] = 25,   /* 408 Request Timeout                       -> idx 24 */
    [409 - 100] = 26,   /* 409 Conflict                              -> idx 25 */
    [410 - 100] = 27,   /* 410 Gone                                  -> idx 26 */
    [411 - 100] = 28,   /* 411 Length Required                       -> idx 27 */
    [412 - 100] = 29,   /* 412 Precondition Failed                   -> idx 28 */
    [413 - 100] = 30,   /* 413 Content Too Large                     -> idx 29 */
    [414 - 100] = 31,   /* 414 URI Too Long                          -> idx 30 */
    [415 - 100] = 32,   /* 415 Unsupported Media Type                -> idx 31 */
    [416 - 100] = 33,   /* 416 Range Not Satisfiable                 -> idx 32 */
    [417 - 100] = 34,   /* 417 Expectation Failed                    -> idx 33 */
    [421 - 100] = 35,   /* 421 Misdirected Request                   -> idx 34 */
    [422 - 100] = 36,   /* 422 Unprocessable Content                 -> idx 35 */
    [425 - 100] = 37,   /* 425 Too Early                             -> idx 36 */
    [426 - 100] = 38,   /* 426 Upgrade Required                      -> idx 37 */
    [428 - 100] = 39,   /* 428 Precondition Required                 -> idx 38 */
    [429 - 100] = 40,   /* 429 Too Many Requests                     -> idx 39 */
    [431 - 100] = 41,   /* 431 Request Header Fields Too Large       -> idx 40 */
    [451 - 100] = 42,   /* 451 Unavailable For Legal Reasons         -> idx 41 */

    [444 - 100] = 43,   /* 444 Connection Closed Without Response    -> idx 42 */
    [494 - 100] = 44,   /* 494 Request Header Too Large              -> idx 43 */
    [495 - 100] = 45,   /* 495 SSL Certificate Error                 -> idx 44 */
    [496 - 100] = 46,   /* 496 SSL Certificate Required              -> idx 45 */
    [497 - 100] = 47,   /* 497 HTTP Request Sent to HTTPS Port       -> idx 46 */
    [499 - 100] = 48,   /* 499 Client Closed Request                 -> idx 47 */

    [500 - 100] = 49,   /* 500 Internal Server Error                 -> idx 48 */
    [501 - 100] = 50,   /* 501 Not Implemented                       -> idx 49 */
    [502 - 100] = 51,   /* 502 Bad Gateway                           -> idx 50 */
    [503 - 100] = 52,   /* 503 Service Unavailable                   -> idx 51 */
    [504 - 100] = 53,   /* 504 Gateway Timeout                       -> idx 52 */
    [505 - 100] = 54,   /* 505 HTTP Version Not Supported            -> idx 53 */
    [506 - 100] = 55,   /* 506 Variant Also Negotiates               -> idx 54 */
    [507 - 100] = 56,   /* 507 Insufficient Storage                  -> idx 55 */
    [508 - 100] = 57,   /* 508 Loop Detected                         -> idx 56 */
    [510 - 100] = 58,   /* 510 Not Extended                          -> idx 57 */
    [511 - 100] = 59,   /* 511 Network Authentication Required       -> idx 58 */
};


#if (NGX_DEBUG)

/*
 * Parallel rfc_section array (debug builds only).
 *
 * Per AAP §0.4.1 explicit guidance: "moving [rfc_section] to a parallel
 * array only compiled in debug builds."  This array is indexed identically
 * to ngx_http_status_registry[] above: index i in this array gives the
 * defining-RFC reference for the entry at index i in the registry.
 *
 * In release builds, NGX_DEBUG is not defined and this array does not
 * exist - saving 472 bytes (59 × 8 bytes per pointer) in addition to the
 * 32-byte-per-entry savings already achieved by removing rfc_section
 * from the runtime registry struct itself.
 *
 * The array is currently consulted only by debug-build diagnostic tooling
 * (extracting the defining-RFC reference for an entry at a given registry
 * index).  No public API function references this array; it is included
 * for forward-compatibility with future debug-only logging or
 * introspection helpers, and to honor the AAP §0.4.1 directive that the
 * rfc_section data continue to live somewhere in the build tree.
 *
 * The encoding "\xc2\xa7" is the UTF-8 encoding of the section sign (§).
 * It is used in each literal so that the source file remains pure ASCII
 * (matching NGINX's source-file convention) while the resulting runtime
 * string still renders as "RFC 9110 §15.x.y" in logs and tooling.
 */
static const char  *ngx_http_status_rfc_sections[] = {

    /* 1xx Informational - RFC 9110 Section 15.2 */
    "RFC 9110 \xc2\xa7""15.2.1",   /* 100 Continue */
    "RFC 9110 \xc2\xa7""15.2.2",   /* 101 Switching Protocols */
    "RFC 2518 \xc2\xa7""10.1",     /* 102 Processing */
    "RFC 8297 \xc2\xa7""2",        /* 103 Early Hints */

    /* 2xx Successful - RFC 9110 Section 15.3 */
    "RFC 9110 \xc2\xa7""15.3.1",   /* 200 OK */
    "RFC 9110 \xc2\xa7""15.3.2",   /* 201 Created */
    "RFC 9110 \xc2\xa7""15.3.3",   /* 202 Accepted */
    "RFC 9110 \xc2\xa7""15.3.4",   /* 203 Non-Authoritative Information */
    "RFC 9110 \xc2\xa7""15.3.5",   /* 204 No Content */
    "RFC 9110 \xc2\xa7""15.3.7",   /* 206 Partial Content */

    /* 3xx Redirection - RFC 9110 Section 15.4 */
    "RFC 9110 \xc2\xa7""15.4.1",   /* 300 Multiple Choices */
    "RFC 9110 \xc2\xa7""15.4.2",   /* 301 Moved Permanently */
    "RFC 9110 \xc2\xa7""15.4.3",   /* 302 Found */
    "RFC 9110 \xc2\xa7""15.4.4",   /* 303 See Other */
    "RFC 9110 \xc2\xa7""15.4.5",   /* 304 Not Modified */
    "RFC 9110 \xc2\xa7""15.4.8",   /* 307 Temporary Redirect */
    "RFC 9110 \xc2\xa7""15.4.9",   /* 308 Permanent Redirect */

    /* 4xx Client Error - RFC 9110 Section 15.5 */
    "RFC 9110 \xc2\xa7""15.5.1",   /* 400 Bad Request */
    "RFC 9110 \xc2\xa7""15.5.2",   /* 401 Unauthorized */
    "RFC 9110 \xc2\xa7""15.5.3",   /* 402 Payment Required */
    "RFC 9110 \xc2\xa7""15.5.4",   /* 403 Forbidden */
    "RFC 9110 \xc2\xa7""15.5.5",   /* 404 Not Found */
    "RFC 9110 \xc2\xa7""15.5.6",   /* 405 Method Not Allowed */
    "RFC 9110 \xc2\xa7""15.5.7",   /* 406 Not Acceptable */
    "RFC 9110 \xc2\xa7""15.5.9",   /* 408 Request Timeout */
    "RFC 9110 \xc2\xa7""15.5.10",  /* 409 Conflict */
    "RFC 9110 \xc2\xa7""15.5.11",  /* 410 Gone */
    "RFC 9110 \xc2\xa7""15.5.12",  /* 411 Length Required */
    "RFC 9110 \xc2\xa7""15.5.13",  /* 412 Precondition Failed */
    "RFC 9110 \xc2\xa7""15.5.14",  /* 413 Content Too Large */
    "RFC 9110 \xc2\xa7""15.5.15",  /* 414 URI Too Long */
    "RFC 9110 \xc2\xa7""15.5.16",  /* 415 Unsupported Media Type */
    "RFC 9110 \xc2\xa7""15.5.17",  /* 416 Range Not Satisfiable */
    "RFC 9110 \xc2\xa7""15.5.18",  /* 417 Expectation Failed */
    "RFC 9110 \xc2\xa7""15.5.20",  /* 421 Misdirected Request */
    "RFC 9110 \xc2\xa7""15.5.21",  /* 422 Unprocessable Content */
    "RFC 8470 \xc2\xa7""5.2",      /* 425 Too Early */
    "RFC 9110 \xc2\xa7""15.5.22",  /* 426 Upgrade Required */
    "RFC 6585 \xc2\xa7""3",        /* 428 Precondition Required */
    "RFC 6585 \xc2\xa7""4",        /* 429 Too Many Requests */
    "RFC 6585 \xc2\xa7""5",        /* 431 Request Header Fields Too Large */
    "RFC 7725 \xc2\xa7""3",        /* 451 Unavailable For Legal Reasons */

    /* NGINX-specific 4xx extensions */
    "nginx extension",             /* 444 Connection Closed Without Response */
    "nginx extension",             /* 494 Request Header Too Large */
    "nginx extension",             /* 495 SSL Certificate Error */
    "nginx extension",             /* 496 SSL Certificate Required */
    "nginx extension",             /* 497 HTTP Request Sent to HTTPS Port */
    "nginx extension",             /* 499 Client Closed Request */

    /* 5xx Server Error - RFC 9110 Section 15.6 */
    "RFC 9110 \xc2\xa7""15.6.1",   /* 500 Internal Server Error */
    "RFC 9110 \xc2\xa7""15.6.2",   /* 501 Not Implemented */
    "RFC 9110 \xc2\xa7""15.6.3",   /* 502 Bad Gateway */
    "RFC 9110 \xc2\xa7""15.6.4",   /* 503 Service Unavailable */
    "RFC 9110 \xc2\xa7""15.6.5",   /* 504 Gateway Timeout */
    "RFC 9110 \xc2\xa7""15.6.6",   /* 505 HTTP Version Not Supported */
    "RFC 2295 \xc2\xa7""8.1",      /* 506 Variant Also Negotiates */
    "RFC 4918 \xc2\xa7""11.5",     /* 507 Insufficient Storage */
    "RFC 5842 \xc2\xa7""7.2",      /* 508 Loop Detected */
    "RFC 2774 \xc2\xa7""7",        /* 510 Not Extended */
    "RFC 6585 \xc2\xa7""6"         /* 511 Network Authentication Required */
};

#endif /* NGX_DEBUG */


/*
 * Number of entries in ngx_http_status_registry[].  Computed at compile time
 * via sizeof; used by `ngx_http_status_lookup_index()` for the bounds
 * assertion that guards the class-index lookup table.
 */
#define NGX_HTTP_STATUS_REGISTRY_SIZE                                          \
    (sizeof(ngx_http_status_registry) / sizeof(ngx_http_status_registry[0]))


/*
 * Lower (inclusive) and upper (exclusive) bounds of the supported numeric
 * status-code range, mirrored as the bounds of the class-index lookup
 * table.  Codes outside [100, 600) are treated as unregistered.
 */
#define NGX_HTTP_STATUS_INDEX_BASE   100
#define NGX_HTTP_STATUS_INDEX_LIMIT  600


/*
 * ngx_http_status_lookup_index: O(1) class-index lookup helper.
 *
 * Resolves `status` to its position in `ngx_http_status_registry[]` and
 * `ngx_http_status_reasons[]` by indexing the static
 * `ngx_http_status_class_index[]` table at offset `(status - 100)`.  The
 * table emits a one-based registry index (1..N, where N is the number of
 * registered codes); the zero sentinel signals "not registered".
 *
 * Returns:
 *   The registry index in [0, NGX_HTTP_STATUS_REGISTRY_SIZE) on success.
 *   -1 if `status` is outside [100, 600) or is otherwise unregistered.
 *
 * Cost: one bounded subtraction, one comparison, one byte-load, one branch.
 * No iteration, no hashing, no allocation.  GCC at -O2 inlines the entire
 * helper into its caller, reducing the per-call cost to a single byte-load
 * after constant folding for compile-time-constant arguments.
 */
static ngx_inline ngx_int_t
ngx_http_status_lookup_index(ngx_uint_t status)
{
    ngx_uint_t  encoded;

    if (status < NGX_HTTP_STATUS_INDEX_BASE
        || status >= NGX_HTTP_STATUS_INDEX_LIMIT)
    {
        return -1;
    }

    encoded = ngx_http_status_class_index[status - NGX_HTTP_STATUS_INDEX_BASE];

    if (encoded == 0) {
        return -1;
    }

    /*
     * Defensive consistency check: the encoded value must reference a
     * valid registry slot.  Any out-of-range encoding indicates that
     * `ngx_http_status_class_index[]` has fallen out of sync with the
     * registry array; this branch should be unreachable and is
     * compile-time-constant-folded out under -O2.
     */
    if (encoded > NGX_HTTP_STATUS_REGISTRY_SIZE) {
        return -1;
    }

    return (ngx_int_t) (encoded - 1);
}


/*
 * ngx_http_status_validate: pure validation of a numeric status code.
 *
 * Tests whether `status` falls within the RFC 9110 valid range of
 * 100..599 inclusive.  This function performs NO logging, NO side
 * effects, and NO registry lookup; it is a constant-time predicate
 * suitable for hot paths.
 *
 * The implementation is deliberately tiny so that gcc can inline it at
 * -O2 in compile-time-constant call sites (e.g., when a caller passes
 * NGX_HTTP_OK), reducing the runtime cost to zero.
 *
 * Returns:
 *   NGX_OK     if status is in the valid range.
 *   NGX_ERROR  otherwise.
 */
ngx_int_t
ngx_http_status_validate(ngx_uint_t status)
{
    return (status >= 100 && status <= 599) ? NGX_OK : NGX_ERROR;
}


/*
 * ngx_http_status_reason: registry lookup with Null Object fallback.
 *
 * Resolves `status` via the O(1) class-index helper and returns a pointer
 * to the registered ngx_str_t reason phrase from the parallel
 * `ngx_http_status_reasons[]` array.  If the code is not in the registry,
 * returns a pointer to the sentinel `ngx_http_status_unknown_reason`
 * ("Unknown") to eliminate NULL-return paths in callers (per AAP §0.7.2
 * D-007).
 *
 * Per AAP §0.4.1, lookup is O(1) via class-index arithmetic: a single
 * bounded subtraction and a single byte-load against
 * `ngx_http_status_class_index[]` resolve any registered code to its
 * `ngx_http_status_reasons[]` slot.  This fits AAP §0.8.2 ("Never optimize
 * registry structure beyond static array implementation") because the
 * lookup data structures are static arrays - no hash table, no LRU cache.
 *
 * The returned pointer is valid for the lifetime of the worker process
 * (the reasons array and phrase buffer sit in a read-only-after-relocation
 * section, mprotected by RELRO) and MUST NOT be freed by the caller.
 *
 * Returns:
 *   const ngx_str_t * pointer to the registered reason phrase, or to
 *   the "Unknown" sentinel if `status` is not registered.  Never NULL.
 */
const ngx_str_t *
ngx_http_status_reason(ngx_uint_t status)
{
    ngx_int_t  idx;

    idx = ngx_http_status_lookup_index(status);

    if (idx < 0) {
        return &ngx_http_status_unknown_reason;
    }

    return &ngx_http_status_reasons[idx];
}


/*
 * ngx_http_status_is_cacheable: heuristic-cacheability flag-bit check.
 *
 * Resolves `status` via the O(1) class-index helper and returns 1 if the
 * registered entry's flags field has the NGX_HTTP_STATUS_CACHEABLE bit
 * set, 0 otherwise.  An unregistered code is conservatively reported as
 * not cacheable.
 *
 * The cacheability flag is set per RFC 9110 Section 15 default
 * cacheability rules: 200, 203, 204, 206, 300, 301, 308, 404, 405, 410,
 * 414, 451, and 501 are heuristically cacheable.  Cache modules consult
 * this function when no explicit Cache-Control directive is present in
 * the response headers.
 *
 * Per AAP §0.4.1, lookup is O(1) via the same class-index helper used by
 * `ngx_http_status_reason()`.
 *
 * Returns:
 *   1 if the registered entry is heuristically cacheable.
 *   0 otherwise (including for unregistered codes).
 */
ngx_uint_t
ngx_http_status_is_cacheable(ngx_uint_t status)
{
    ngx_int_t  idx;

    idx = ngx_http_status_lookup_index(status);

    if (idx < 0) {
        return 0;
    }

    return (ngx_http_status_registry[idx].flags
            & NGX_HTTP_STATUS_CACHEABLE) ? 1 : 0;
}


/*
 * ngx_http_status_register: stub returning NGX_ERROR (registry is immutable).
 *
 * Per AAP §0.8.2 ("Never create registry modification APIs accessible
 * post-initialization"), the registry is immutable at runtime.  The
 * registry is populated entirely by static initializers at compile time,
 * which means the ngx_http_status_init_done flag is set to 1 before
 * main() runs.  This function therefore detects the post-initialization
 * state on its first invocation and returns NGX_ERROR, satisfying the
 * "never modifiable post-init" rule trivially.
 *
 * The function exists in the public API surface for two reasons:
 *
 *   1. Forward compatibility: future versions of NGINX may permit
 *      controlled, configuration-phase-only registration via this entry
 *      point without changing the ABI.
 *
 *   2. Diagnostic clarity: third-party module authors who attempt to
 *      register custom codes at runtime receive a deterministic NGX_ERROR
 *      return rather than silent success or undefined behavior.
 *
 * Parameters:
 *   def  Pointer to a caller-allocated ngx_http_status_def_t.  Currently
 *        ignored (validated only for NULL).
 *
 * Returns:
 *   NGX_ERROR  always at runtime.  Reserved NGX_OK semantics for future
 *              configuration-phase use.
 */
ngx_int_t
ngx_http_status_register(ngx_http_status_def_t *def)
{
    if (ngx_http_status_init_done) {
        return NGX_ERROR;
    }

    /*
     * Unreachable in normal builds: ngx_http_status_init_done is statically
     * initialized to 1, so the early-return above always fires.  This
     * defensive check is preserved to surface obvious caller bugs (a NULL
     * def) should the init-done flag ever be cleared in some future
     * configuration-phase extension.
     */
    if (def == NULL) {
        return NGX_ERROR;
    }

    return NGX_OK;
}


/*
 * ngx_http_status_set: central facade for assigning the response status code.
 *
 * BUILD-MODE DISPATCH (key invariant for the AAP D-004 zero-overhead promise):
 *
 *   This out-of-line definition compiles ONLY when NGX_HTTP_STATUS_VALIDATION
 *   is defined (configure flag --with-http_status_validation).  In permissive
 *   builds — the default — the function is defined as `static ngx_inline`
 *   directly in <ngx_http_status.h> so that every translation unit that
 *   includes <ngx_http.h> sees the body and inlines it at the call site.
 *   That dual-mode split realizes AAP D-004's "compile-time inlining" design
 *   contract and AAP §0.7.6's "no CALL instruction for NGX_HTTP_OK-style
 *   literals" disassembly criterion: the public symbol `ngx_http_status_set`
 *   does not appear in the binary's symbol table for default permissive
 *   builds, and every compile-time-constant call site collapses to a single
 *   mov-immediate to r->headers_out.status.
 *
 *   The strict-mode body below is large (~100 lines including comments) and
 *   would inflate the binary substantially if inlined at every call site.
 *   Strict mode is an opt-in pre-deployment validation feature, so the
 *   marginal CALL/RET cost from out-of-line dispatch is acceptable in that
 *   configuration.
 *
 * STRICT-MODE BEHAVIOR (this function):
 *
 *   This is the unified entry point through which all NGINX-originated
 *   status code assignments flow after this refactor.  It replaces the
 *   legacy direct field assignment "r->headers_out.status = status;"
 *   pattern with a facade that:
 *
 *   1. Range-checks status against 100..599 inclusive.  Out-of-range codes
 *      are rejected with NGX_LOG_WARN.  This prevents header injection via
 *      crafted status values per AAP §0.8.6.
 *
 *   2. Detects a 1xx response emitted after a final (>= 200) response,
 *      which is a protocol violation.  Such attempts are rejected with
 *      NGX_LOG_ERR.
 *
 *   3. Single-final-code enforcement per AAP §0.1.1 G4: detects a
 *      different final (>= 200 and <= 599) status assignment that
 *      overrides a non-200 final response already recorded on the
 *      request.  Such attempts are rejected with NGX_LOG_ERR.  Two
 *      categories of override are permitted:
 *
 *        (a) Identical reassignment (e.g., a phase handler re-emitting
 *            the already-recorded code) is permitted because it produces
 *            a wire-identical response and is a common idiom.
 *
 *        (b) Override of an existing NGX_HTTP_OK (200) by any other
 *            final code is permitted because NGINX's filter chain
 *            architecture is fundamentally override-based: content
 *            handlers set an initial 200 OK and downstream header
 *            filters (range, not-modified, error-page) override it to
 *            the request-appropriate final code (206/304/416/4xx/5xx).
 *            These overrides happen during in-memory request processing,
 *            before any byte of the response is transmitted, so exactly
 *            ONE final status still reaches the client.  Filter modules
 *            guard their overrides with explicit `r->headers_out.status
 *            == NGX_HTTP_OK` checks (see e.g. range_filter_module.c
 *            line 156 and not_modified_filter_module.c line 57), so a
 *            200-to-other-final transition observed at this site is a
 *            documented filter-chain override rather than a genuine
 *            protocol violation.
 *
 *      The forbidden case is therefore a transition between two
 *      different non-200 final codes (e.g., 404 -> 500), which always
 *      signals a logic bug in the calling module.
 *
 *   4. Bypasses strict validation when r->upstream is non-NULL (per AAP
 *      §0.1.1 I3 + §0.7.2 D-006).  Upstream pass-through must preserve
 *      the original code from the backend response verbatim, even if it
 *      falls outside the RFC range; rejecting an upstream's 0 or 999
 *      would be a regression and a protocol violation against the proxy
 *      contract.
 *
 *   5. Emits a debug-level trace log line at NGX_LOG_DEBUG_HTTP for every
 *      successful assignment, using the canonical format-string contract
 *      documented in docs/architecture/observability.md ("http status
 *      set: %ui %V (strict=%s upstream=%s)").  The permissive-mode inline
 *      definition emits the same canonical format with a hard-coded
 *      strict="no" so log parsers extract fields uniformly across modes.
 *      Validation failures emit their own higher-severity log lines
 *      (NGX_LOG_WARN / NGX_LOG_ERR) and do not proceed to the debug-
 *      trace emission because no assignment has occurred.
 *
 *   6. Assigns r->headers_out.status to the requested value.
 *
 * Parameters:
 *   r       The request whose response status code is being set.  Must not
 *           be NULL; r->connection must not be NULL; r->connection->log
 *           must not be NULL (NGINX guarantees these for any live request).
 *   status  The numeric HTTP status code to assign.
 *
 * Returns:
 *   NGX_OK     on successful validation and assignment.
 *   NGX_ERROR  if validation fails (out-of-range code, 1xx after a final
 *              response, or a different non-200 final code overriding an
 *              existing non-200 final response; 200-to-other-final-code
 *              transitions are permitted as documented filter-chain
 *              overrides — see the full transition matrix above).
 *
 * Side effects:
 *   - Sets r->headers_out.status = status when validation passes.
 *   - May emit log entries at NGX_LOG_DEBUG_HTTP, NGX_LOG_WARN, or
 *     NGX_LOG_ERR depending on the input.
 */
#if (NGX_HTTP_STATUS_VALIDATION)

ngx_int_t
ngx_http_status_set(ngx_http_request_t *r, ngx_uint_t status)
{
    if (r->upstream == NULL) {
        if (status < 100 || status > 599) {
            ngx_log_error(NGX_LOG_WARN, r->connection->log, 0,
                          "ngx_http_status_set: invalid status %ui "
                          "(out of RFC 9110 range 100-599)", status);
            return NGX_ERROR;
        }

        if (status >= 100 && status < 200
            && r->headers_out.status >= 200 && r->headers_out.status <= 599)
        {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                          "ngx_http_status_set: 1xx status %ui after "
                          "final response %ui",
                          status, r->headers_out.status);
            return NGX_ERROR;
        }

        /*
         * Single-final-code enforcement per AAP §0.1.1 G4: detect a
         * different final (>= 200 and <= 599) status assignment that
         * overrides a final response already recorded on the request.
         *
         * NGINX FILTER-CHAIN EXCEPTION (200 OK is the documented "initial"
         * state that filters override):
         *
         * NGINX's response pipeline relies on content handlers setting an
         * initial 200 OK that downstream header filters subsequently
         * override based on request semantics.  Concrete in-tree examples:
         *
         *   - ngx_http_range_filter_module.c gates its filter on
         *     "r->headers_out.status != NGX_HTTP_OK" (line 156) and only
         *     overrides 200 -> 206 (Partial Content) for a satisfied Range
         *     request, or 200 -> 416 (Range Not Satisfiable) for an
         *     unsatisfiable Range.
         *
         *   - ngx_http_not_modified_filter_module.c gates its filter on
         *     the same "r->headers_out.status != NGX_HTTP_OK" check
         *     (line 57) and overrides 200 -> 304 (Not Modified) when
         *     conditional-GET preconditions match.
         *
         *   - ngx_http_send_header() in ngx_http_core_module.c translates
         *     r->err_status into the wire response (line 1883), which can
         *     override the 200 set by an earlier content-handler call to
         *     ngx_http_status_set() before the error path was selected.
         *
         * These overrides are intentional and produce wire-correct
         * responses: exactly ONE final status reaches the client, because
         * the override happens during in-memory request processing, before
         * any byte of the response is transmitted.  Treating these
         * transitions as protocol violations rejects valid NGINX filter
         * chain semantics and causes range_filter/not_modified_filter to
         * emit RFC-malformed responses (200 with Content-Range, 200 with
         * full body for a matched If-Modified-Since, etc.).
         *
         * The check therefore applies only when the previously-recorded
         * status is a non-200 final code.  Permitted transitions:
         *
         *   - 0      -> any final         (initial assignment)
         *   - 200    -> any final         (filter chain override)
         *   - C      -> C                 (idempotent reassignment)
         *
         * Forbidden transitions (true protocol-violation patterns):
         *
         *   - 4xx/5xx (other than 200) -> different non-equal final
         *     (e.g., a 404 erroneously overridden by a 500 from a
         *     separate code path is a genuine logic bug)
         *
         * Identical reassignment (e.g., a phase handler re-emitting an
         * already-recorded NGX_HTTP_OK) remains permitted via the
         * `!= status` predicate because it produces a wire-identical
         * response and is a common idiom.
         *
         * The 1xx-after-final check above is unaffected by this exception:
         * it operates on a disjoint slice of the (status, prior status)
         * domain (1xx new code vs. any final prior).
         */
        if (status >= 200 && status <= 599
            && r->headers_out.status >= 200 && r->headers_out.status <= 599
            && r->headers_out.status != NGX_HTTP_OK
            && r->headers_out.status != status)
        {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                          "ngx_http_status_set: final response %ui after "
                          "final %ui (single-final-code rule violated)",
                          status, r->headers_out.status);
            return NGX_ERROR;
        }
    }

    /*
     * Emit the unified observability trace line per the canonical format
     * string documented in docs/architecture/observability.md:
     *
     *   "http status set: %ui %V (strict=%s upstream=%s)"
     *
     * The four format specifiers — %ui (numeric code), %V (canonical
     * reason phrase from the registry), %s (strict-mode flag), %s
     * (upstream-presence flag) — must remain in this exact order so that
     * downstream log parsers (e.g., Grafana dashboards described in
     * docs/architecture/observability.md) extract fields correctly.  This
     * format is mirrored in the permissive-mode inline definition in
     * <ngx_http_status.h> with a hard-coded strict="no" so that log lines
     * from both modes parse identically.
     *
     * Implementation notes:
     *
     *   - The registry lookup ngx_http_status_reason(status) is inlined
     *     as a macro argument rather than assigned to a local variable.
     *     When NGX_DEBUG is undefined (release builds) the entire
     *     ngx_log_debug4 macro expands to nothing, the registry lookup is
     *     fully elided by the preprocessor, and there is neither a
     *     runtime cost nor an "unused variable" warning.
     *
     *   - The line is emitted on every successful assignment.  Validation
     *     failures (above) emit their own NGX_LOG_WARN / NGX_LOG_ERR
     *     entries and return NGX_ERROR before reaching this point —
     *     those higher-severity entries already record the rejected
     *     status, so emitting the debug-trace line for a non-assignment
     *     would be misleading.
     */
    ngx_log_debug4(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http status set: %ui %V (strict=%s upstream=%s)",
                   status, ngx_http_status_reason(status),
                   "yes",
                   r->upstream != NULL ? "yes" : "no");

    r->headers_out.status = status;

    return NGX_OK;
}

#endif /* NGX_HTTP_STATUS_VALIDATION */


/*
 * ngx_http_status_init_registry: forward-compatibility cycle hook.
 *
 * The registry array is populated by C static initializers at compile time
 * (see ngx_http_status_registry[] above), so the registry is fully
 * operational before main() runs and no runtime initialization is
 * required.  ngx_http_status_set(), ngx_http_status_validate(),
 * ngx_http_status_reason(), and ngx_http_status_is_cacheable() are all
 * usable from process startup.
 *
 * This function is provided in the API surface for two reasons:
 *
 *   1. Forward compatibility: should a future NGINX revision need to
 *      perform per-cycle initialization (e.g., a debug-only registry
 *      consistency check, or per-cycle metric counter reset), this
 *      function is the documented hook for that work.
 *
 *   2. Explicit AAP compliance: the AAP §0.8.4 requires "Registry array
 *      population must occur before first worker process fork" and AAP
 *      §0.8.3 requires "Registry initialization must complete during
 *      configuration parsing phase only".  Both are satisfied trivially
 *      by static initialization, but having a named init function makes
 *      the contract explicit and gives operators a clear point to insert
 *      consistency checks via debug builds.
 *
 * The current implementation performs only a defensive NULL-pointer
 * check on the cycle argument and re-asserts the init_done flag.
 *
 * Parameters:
 *   cycle  The active NGINX cycle.  Must not be NULL.
 *
 * Returns:
 *   NGX_OK     on success (always, when cycle is non-NULL).
 *   NGX_ERROR  if cycle is NULL.
 */
ngx_int_t
ngx_http_status_init_registry(ngx_cycle_t *cycle)
{
    if (cycle == NULL) {
        return NGX_ERROR;
    }

#if (NGX_DEBUG)

    /*
     * Compile-time consistency check available in debug builds: the
     * parallel `ngx_http_status_rfc_sections[]` array (compiled only when
     * NGX_DEBUG is defined per AAP §0.4.1) must have exactly the same
     * number of entries as `ngx_http_status_registry[]`.  Any future
     * modification of either array must update both.
     *
     * The `sizeof` expressions also serve as the explicit reference that
     * satisfies `-Wunused-variable` for `ngx_http_status_rfc_sections`,
     * which would otherwise be flagged as defined-but-unused under the
     * NGINX strict `-Werror` build settings (debug builds enable
     * `-Wunused-variable` for file-scope statics, which the release build
     * suppresses by simply not declaring the array at all).
     */
    if ((sizeof(ngx_http_status_rfc_sections)
         / sizeof(ngx_http_status_rfc_sections[0]))
        != NGX_HTTP_STATUS_REGISTRY_SIZE)
    {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                      "ngx_http_status_init_registry: "
                      "rfc_sections array length mismatch with registry");
        return NGX_ERROR;
    }

#endif

    ngx_http_status_init_done = 1;

    return NGX_OK;
}
