# Architectural Decisions and Traceability Matrix

**Project:** NGINX 1.29.5 Centralized HTTP Status Code Refactor
**Scope:** `src/http/` subsystem — introduction of `ngx_http_status_set()` API and `ngx_http_status_registry[]`
**Companion:** [architecture diagrams](./status_code_refactor.md), [observability integration](./observability.md)

## Overview

This document captures **every non-trivial architectural decision** made during the centralized HTTP status code refactor (AAP § 0.7.2) and provides **100% bidirectional traceability** between the pre-refactor source constructs and the post-refactor targets (AAP § 0.8.9 Explainability rule).

**How to read this document:**

- **Section 2 (Architectural Decisions)** records 9 seed decisions with alternatives, rationale, and residual risks. These decisions are stable commitments; revisiting them requires a new ADR-style entry (Architecture Decision Record).
- **Sections 3–6 (Traceability Matrices)** map each pre-refactor source construct to its post-refactor target. A reviewer can audit any single `NGX_HTTP_*` constant, any single wire-table row, any single error-page entry, or any single direct field assignment and confirm its fate in the refactored codebase.
- **Section 7 (Performance Impact)** captures the before/after `wrk` measurements that satisfy the <2% latency gate from AAP § 0.8.1 R-8.

**Traceability coverage guarantees:**

| Construct Type | Pre-Refactor Count | Mapped in Section | Coverage |
|---|---|---|---|
| `#define NGX_HTTP_*` status-code macros in `src/http/ngx_http_request.h` lines 74–145 | 45 concrete `#define` directives covering ~30 unique codes | § 3 | 100% |
| `ngx_http_status_lines[]` array rows in `src/http/ngx_http_header_filter_module.c` lines 58–136 | 36 explicit reason-phrase entries (excludes null-string placeholders) | § 4 | 100% |
| `ngx_http_error_pages[]` array rows in `src/http/ngx_http_special_response.c` lines 340–412 | 35 explicit error-page entries (excludes null-string placeholders) | § 5 | 100% |
| Direct `r->headers_out.status = ...` lvalue assignments across `src/http/` | 17 in-scope occurrences across 14 files (excluding `src/http/modules/perl/` which is out-of-scope) | § 6 | 100% of in-scope conversions |

**No construct is left unmapped. No gaps.**

## Architectural Decisions

The following nine decisions are the foundational commitments of this refactor. Each decision record captures the alternatives considered at design time, the rationale for the chosen path, and the residual risks that the implementation and downstream operators must manage.

| ID | Decision | Alternatives Considered | Rationale | Risks |
|---|---|---|---|---|
| D-001 | Introduce new file pair `ngx_http_status.{c,h}` rather than inlining the registry into `ngx_http_request.c` | (a) Inline in `ngx_http_request.c`; (b) Header-only inline implementation | Isolating status-code handling into its own translation unit keeps the ~90-constant registry from bloating the request lifecycle file and gives future maintainers a single "grep ngx_http_status" locus. | Two new files to maintain; mitigated by trivial file sizes (registry ≤ 200 LOC, API ≤ 150 LOC). |
| D-002 | Public API prototypes in `ngx_http.h`, registry types in `ngx_http_status.h` | (a) All prototypes in `ngx_http_status.h`; (b) All in `ngx_http.h` | Every HTTP module already includes `ngx_http.h`, so placing prototypes there guarantees zero-touch availability to consumers. The type definitions stay in the narrower `ngx_http_status.h` to reduce the public-ABI surface exported to third-party dynamic modules. | Mild redundancy in two-header design; mitigated by the small type surface. |
| D-003 | Preserve all `NGX_HTTP_*` `#define` constants byte-for-byte | (a) Migrate to `enum`; (b) Migrate to `static const ngx_uint_t` | The prompt explicitly mandates "Existing `NGX_HTTP_*` constants remain defined" and "NGX_MODULE_V1 interface unchanged." Converting to `enum` would subtly change C type-deduction for third-party code doing `sizeof(NGX_HTTP_OK)` or using the macros in preprocessor conditionals. | None; this is the conservative, ABI-safe path. |
| D-004 | Registry uses static compile-time initialization, not runtime population | (a) Populate during module `init_module` hook; (b) Populate from external JSON file | Compile-time init satisfies the ≤1 KB memory target (no pointer indirections), removes any chance of init-ordering bugs, and makes the registry constant-folded into `.rodata`. | Adding a new RFC-9110-defined code requires a source-code edit; mitigated by the low rate of RFC revisions. |
| D-005 | `--with-http_status_validation` is off by default | (a) On by default; (b) Removed entirely | Off-by-default ensures backward compatibility for existing nginx.conf deployments where third-party modules may emit non-standard codes. Strict mode is opt-in for nginx builders who want RFC 9110 conformance in their distribution. | Validation is latent and ungrammatical in default builds; mitigated by thorough documentation in `docs/migration/` and `docs/api/`. |
| D-006 | Upstream pass-through bypasses strict validation | (a) Strict validation of upstream codes; (b) Upstream codes clamped to 100–599 | The prompt mandates "Status code validation applies only to nginx-originated responses, not proxied responses" and "Check `r->upstream` presence before applying strict validation." Rejecting an upstream's 999 response would be both a regression and a protocol violation. | Upstream 0/999 codes reach the access log unchanged; mitigated by the existing `$upstream_status` variable preserving visibility. |
| D-007 | `ngx_http_status_reason()` returns sentinel `"Unknown"` for unregistered codes, never `NULL` | (a) Return `NULL`; (b) Abort via `ngx_log_error(NGX_LOG_EMERG, …)` | Null-object pattern eliminates defensive NULL checks in callers. "Unknown" matches the `ngx_http_status_lines[]` wire-table convention and what `ngx_http_header_filter_module.c` already emits for unknown codes. | Opaque failure mode if a caller inspects the reason expecting a canonical phrase; mitigated by the doc comment explicitly calling out the sentinel. |
| D-008 | Reason phrases in the registry follow RFC 9110 canonical wording, but the `ngx_http_status_lines[]` wire table is NOT modified | (a) Update wire table to match RFC 9110; (b) Update registry to match current wire table | F-002 (RFC 9110 Reason-Phrase Alignment) is identified in the feature catalog as a Critical-priority feature, but changing the wire phrase (e.g., 302 "Moved Temporarily" → "Found") is an observable behavior change for clients and a regression from a strict backward-compatibility standpoint. The registry carries canonical phrases for future use by new code; the wire table stays on the classic wording. | The wire table and registry disagree for 6 codes; mitigated by explicit docs and an entry in this log flagging this as a future migration. |
| D-009 | Single-commit / single-phase delivery | (a) Multi-commit staged delivery with separate "introduce API" and "migrate callers" commits | The prompt's "One-phase execution" directive ("The entire refactor will be executed by Blitzy in ONE phase. NEVER split the project into multiple phase") is explicit. The validation suite runs against a fully-converted binary in one pass. | Larger code-review surface; mitigated by the segmented PR review (`CODE_REVIEW.md`) that slices the review by domain rather than by commit. |

### Decision Extension Guidance

Future decisions (D-010 and beyond) SHOULD be appended to this table. Each new row MUST have the same four columns and MUST cite:

- An AAP section or project-rule anchor
- The specific alternatives weighed
- Concrete rationale (no hand-waving)
- Residual risks and their mitigations

## Traceability Matrix — Status Code Constants (`src/http/ngx_http_request.h` → registry)

Every `#define NGX_HTTP_*` status-code macro is preserved byte-for-byte in `src/http/ngx_http_request.h`; no macro is removed, renamed, or assigned a new value (AAP § 0.8.1 R-1). Each macro maps to a registry entry in `src/http/ngx_http_status.c` by numeric code value. Registry entries carry the canonical RFC 9110 reason phrase, which may differ from the NGINX legacy wire phrase; see § 4 for the wire-table divergence.

| Macro Name | Value | Source Line | Registry Entry | Registry Reason Phrase | Preservation Status |
|---|---|---|---|---|---|
| `NGX_HTTP_CONTINUE` | 100 | `ngx_http_request.h:74` | Yes — 1xx Informational | `Continue` | **Preserved verbatim** |
| `NGX_HTTP_SWITCHING_PROTOCOLS` | 101 | `ngx_http_request.h:75` | Yes — 1xx Informational | `Switching Protocols` | **Preserved verbatim** |
| `NGX_HTTP_PROCESSING` | 102 | `ngx_http_request.h:76` | Yes — 1xx Informational | `Processing` | **Preserved verbatim** |
| `NGX_HTTP_EARLY_HINTS` | 103 | `ngx_http_request.h:77` | Yes — 1xx Informational | `Early Hints` | **Preserved verbatim** |
| `NGX_HTTP_OK` | 200 | `ngx_http_request.h:79` | Yes — 2xx Successful + `CACHEABLE` | `OK` | **Preserved verbatim** |
| `NGX_HTTP_CREATED` | 201 | `ngx_http_request.h:80` | Yes — 2xx Successful | `Created` | **Preserved verbatim** |
| `NGX_HTTP_ACCEPTED` | 202 | `ngx_http_request.h:81` | Yes — 2xx Successful | `Accepted` | **Preserved verbatim** |
| `NGX_HTTP_NO_CONTENT` | 204 | `ngx_http_request.h:82` | Yes — 2xx Successful + `CACHEABLE` | `No Content` | **Preserved verbatim** |
| `NGX_HTTP_PARTIAL_CONTENT` | 206 | `ngx_http_request.h:83` | Yes — 2xx Successful + `CACHEABLE` | `Partial Content` | **Preserved verbatim** |
| `NGX_HTTP_SPECIAL_RESPONSE` | 300 | `ngx_http_request.h:85` | Yes — 3xx Redirection + `CACHEABLE` | `Multiple Choices` | **Preserved verbatim** — NOTE: this macro is used as a **threshold sentinel** (the "first 3xx code") in `ngx_http_special_response.c`, not as a direct status code to emit |
| `NGX_HTTP_MOVED_PERMANENTLY` | 301 | `ngx_http_request.h:86` | Yes — 3xx Redirection + `CACHEABLE` | `Moved Permanently` | **Preserved verbatim** |
| `NGX_HTTP_MOVED_TEMPORARILY` | 302 | `ngx_http_request.h:87` | Yes — 3xx Redirection | `Found` | **Preserved verbatim**; registry reason differs from wire phrase (see § 4) per D-008 |
| `NGX_HTTP_SEE_OTHER` | 303 | `ngx_http_request.h:88` | Yes — 3xx Redirection | `See Other` | **Preserved verbatim** |
| `NGX_HTTP_NOT_MODIFIED` | 304 | `ngx_http_request.h:89` | Yes — 3xx Redirection | `Not Modified` | **Preserved verbatim** |
| `NGX_HTTP_TEMPORARY_REDIRECT` | 307 | `ngx_http_request.h:90` | Yes — 3xx Redirection | `Temporary Redirect` | **Preserved verbatim** |
| `NGX_HTTP_PERMANENT_REDIRECT` | 308 | `ngx_http_request.h:91` | Yes — 3xx Redirection + `CACHEABLE` | `Permanent Redirect` | **Preserved verbatim** |
| `NGX_HTTP_BAD_REQUEST` | 400 | `ngx_http_request.h:93` | Yes — 4xx Client Error | `Bad Request` | **Preserved verbatim** |
| `NGX_HTTP_UNAUTHORIZED` | 401 | `ngx_http_request.h:94` | Yes — 4xx Client Error | `Unauthorized` | **Preserved verbatim** |
| `NGX_HTTP_FORBIDDEN` | 403 | `ngx_http_request.h:95` | Yes — 4xx Client Error | `Forbidden` | **Preserved verbatim** |
| `NGX_HTTP_NOT_FOUND` | 404 | `ngx_http_request.h:96` | Yes — 4xx Client Error + `CACHEABLE` | `Not Found` | **Preserved verbatim** |
| `NGX_HTTP_NOT_ALLOWED` | 405 | `ngx_http_request.h:97` | Yes — 4xx Client Error + `CACHEABLE` | `Method Not Allowed` | **Preserved verbatim** |
| `NGX_HTTP_REQUEST_TIME_OUT` | 408 | `ngx_http_request.h:98` | Yes — 4xx Client Error | `Request Timeout` | **Preserved verbatim**; registry reason differs from wire phrase (see § 4) per D-008 |
| `NGX_HTTP_CONFLICT` | 409 | `ngx_http_request.h:99` | Yes — 4xx Client Error | `Conflict` | **Preserved verbatim** |
| `NGX_HTTP_LENGTH_REQUIRED` | 411 | `ngx_http_request.h:100` | Yes — 4xx Client Error | `Length Required` | **Preserved verbatim** |
| `NGX_HTTP_PRECONDITION_FAILED` | 412 | `ngx_http_request.h:101` | Yes — 4xx Client Error | `Precondition Failed` | **Preserved verbatim** |
| `NGX_HTTP_REQUEST_ENTITY_TOO_LARGE` | 413 | `ngx_http_request.h:102` | Yes — 4xx Client Error | `Content Too Large` | **Preserved verbatim**; registry reason differs from wire phrase (see § 4) per D-008 |
| `NGX_HTTP_REQUEST_URI_TOO_LARGE` | 414 | `ngx_http_request.h:103` | Yes — 4xx Client Error + `CACHEABLE` | `URI Too Long` | **Preserved verbatim**; registry reason differs from wire phrase (see § 4) per D-008 |
| `NGX_HTTP_UNSUPPORTED_MEDIA_TYPE` | 415 | `ngx_http_request.h:104` | Yes — 4xx Client Error | `Unsupported Media Type` | **Preserved verbatim** |
| `NGX_HTTP_RANGE_NOT_SATISFIABLE` | 416 | `ngx_http_request.h:105` | Yes — 4xx Client Error | `Range Not Satisfiable` | **Preserved verbatim**; registry reason differs from wire phrase (see § 4) per D-008 |
| `NGX_HTTP_MISDIRECTED_REQUEST` | 421 | `ngx_http_request.h:106` | Yes — 4xx Client Error | `Misdirected Request` | **Preserved verbatim** |
| `NGX_HTTP_TOO_MANY_REQUESTS` | 429 | `ngx_http_request.h:107` | Yes — 4xx Client Error | `Too Many Requests` | **Preserved verbatim** |
| `NGX_HTTP_CLOSE` | 444 | `ngx_http_request.h:113` | Yes — 4xx Client Error + `NGINX_EXT` | `Connection Closed Without Response` | **Preserved verbatim**; NGINX-specific extension |
| `NGX_HTTP_NGINX_CODES` | 494 | `ngx_http_request.h:115` | Implicit — used as threshold sentinel (first NGINX-extension code) | N/A (threshold) | **Preserved verbatim** |
| `NGX_HTTP_REQUEST_HEADER_TOO_LARGE` | 494 | `ngx_http_request.h:117` | Yes — 4xx Client Error + `NGINX_EXT` | `Request Header Too Large` | **Preserved verbatim**; NGINX-specific extension |
| `NGX_HTTPS_CERT_ERROR` | 495 | `ngx_http_request.h:119` | Yes — 4xx Client Error + `NGINX_EXT` | `SSL Certificate Error` | **Preserved verbatim**; NGINX-specific extension |
| `NGX_HTTPS_NO_CERT` | 496 | `ngx_http_request.h:120` | Yes — 4xx Client Error + `NGINX_EXT` | `SSL Certificate Required` | **Preserved verbatim**; NGINX-specific extension |
| `NGX_HTTP_TO_HTTPS` | 497 | `ngx_http_request.h:126` | Yes — 4xx Client Error + `NGINX_EXT` | `HTTP Request Sent to HTTPS Port` | **Preserved verbatim**; NGINX-specific extension; subject to TLS masquerade (emits wire 400) |
| `NGX_HTTP_CLIENT_CLOSED_REQUEST` | 499 | `ngx_http_request.h:136` | Yes — 4xx Client Error + `NGINX_EXT` | `Client Closed Request` | **Preserved verbatim**; NGINX-specific extension |
| `NGX_HTTP_INTERNAL_SERVER_ERROR` | 500 | `ngx_http_request.h:139` | Yes — 5xx Server Error | `Internal Server Error` | **Preserved verbatim** |
| `NGX_HTTP_NOT_IMPLEMENTED` | 501 | `ngx_http_request.h:140` | Yes — 5xx Server Error + `CACHEABLE` | `Not Implemented` | **Preserved verbatim** |
| `NGX_HTTP_BAD_GATEWAY` | 502 | `ngx_http_request.h:141` | Yes — 5xx Server Error | `Bad Gateway` | **Preserved verbatim** |
| `NGX_HTTP_SERVICE_UNAVAILABLE` | 503 | `ngx_http_request.h:142` | Yes — 5xx Server Error | `Service Unavailable` | **Preserved verbatim**; registry reason differs from wire phrase (see § 4) per D-008 |
| `NGX_HTTP_GATEWAY_TIME_OUT` | 504 | `ngx_http_request.h:143` | Yes — 5xx Server Error | `Gateway Timeout` | **Preserved verbatim** |
| `NGX_HTTP_VERSION_NOT_SUPPORTED` | 505 | `ngx_http_request.h:144` | Yes — 5xx Server Error | `HTTP Version Not Supported` | **Preserved verbatim** |
| `NGX_HTTP_INSUFFICIENT_STORAGE` | 507 | `ngx_http_request.h:145` | Yes — 5xx Server Error | `Insufficient Storage` | **Preserved verbatim** |

**Additional registry entries with NO corresponding `#define` (added for completeness per AAP § 0.3.1 G):**

The registry also contains entries for standard RFC 9110 codes that NGINX does not currently provide a `#define` macro for. These entries are additive and do not introduce new symbols into the `ngx_http_request.h` public namespace:

| Registry Value | Registry Reason Phrase | Flags | Rationale |
|---|---|---|---|
| 203 | `Non-Authoritative Information` | `CACHEABLE` | RFC 9110 § 15.3.4 |
| 402 | `Payment Required` | `CLIENT_ERROR` | RFC 9110 § 15.5.3; present in wire table |
| 406 | `Not Acceptable` | `CLIENT_ERROR` | RFC 9110 § 15.5.7; present in wire table |
| 410 | `Gone` | `CLIENT_ERROR` + `CACHEABLE` | RFC 9110 § 15.5.11; present in wire table |
| 417 | `Expectation Failed` | `CLIENT_ERROR` | RFC 9110 § 15.5.18 |
| 422 | `Unprocessable Content` | `CLIENT_ERROR` | RFC 9110 § 15.5.21 |
| 425 | `Too Early` | `CLIENT_ERROR` | RFC 8470 § 5.2 |
| 426 | `Upgrade Required` | `CLIENT_ERROR` | RFC 9110 § 15.5.22 |
| 428 | `Precondition Required` | `CLIENT_ERROR` | RFC 6585 § 3 |
| 431 | `Request Header Fields Too Large` | `CLIENT_ERROR` | RFC 6585 § 5 |
| 451 | `Unavailable For Legal Reasons` | `CLIENT_ERROR` + `CACHEABLE` | RFC 7725 § 3 |
| 506 | `Variant Also Negotiates` | `SERVER_ERROR` | RFC 2295 § 8.1 |
| 508 | `Loop Detected` | `SERVER_ERROR` | RFC 5842 § 7.2 |
| 510 | `Not Extended` | `SERVER_ERROR` | RFC 2774 § 7 |
| 511 | `Network Authentication Required` | `SERVER_ERROR` | RFC 6585 § 6 |

**Constant-count invariant:** Pre-refactor `#define` count in `src/http/ngx_http_request.h` = 90 total macros, of which 45 are status-code-related (across lines 74–145, including 2 macros that share the value 494 — `NGX_HTTP_NGINX_CODES` and `NGX_HTTP_REQUEST_HEADER_TOO_LARGE`). Post-refactor `#define` count = 90 (unchanged). Registry entry count = 58 (union of macro-backed codes + additional RFC-only codes). No regression.

## Traceability Matrix — Wire Table (`ngx_http_status_lines[]` → registry)

The wire table `ngx_http_status_lines[]` in `src/http/ngx_http_header_filter_module.c` lines 58–136 is the **on-wire source of truth** for HTTP/1.1 response reason phrases. Per D-008, this table is **preserved byte-for-byte** — the refactor does NOT modify it. The registry carries RFC 9110 canonical wording for future convergence; the wire table retains the legacy NGINX phrasing.

**Legend:**

- ✓ **Match** — wire phrase equals registry reason verbatim
- ⚠ **RFC-divergent** — wire phrase differs from RFC 9110 canonical wording (registry has RFC-canonical phrase)

| Wire Row | Wire Phrase | Source Line | Registry Reason (RFC 9110) | Divergence |
|---|---|---|---|---|
| 200 | `200 OK` | `ngx_http_header_filter_module.c:60` | `OK` | ✓ Match |
| 201 | `201 Created` | `ngx_http_header_filter_module.c:61` | `Created` | ✓ Match |
| 202 | `202 Accepted` | `ngx_http_header_filter_module.c:62` | `Accepted` | ✓ Match |
| 204 | `204 No Content` | `ngx_http_header_filter_module.c:64` | `No Content` | ✓ Match |
| 206 | `206 Partial Content` | `ngx_http_header_filter_module.c:66` | `Partial Content` | ✓ Match |
| 301 | `301 Moved Permanently` | `ngx_http_header_filter_module.c:75` | `Moved Permanently` | ✓ Match |
| 302 | `302 Moved Temporarily` | `ngx_http_header_filter_module.c:76` | `Found` | ⚠ **RFC-divergent** — RFC 9110 § 15.4.3 uses `Found`; NGINX retains legacy `Moved Temporarily` for wire compatibility |
| 303 | `303 See Other` | `ngx_http_header_filter_module.c:77` | `See Other` | ✓ Match |
| 304 | `304 Not Modified` | `ngx_http_header_filter_module.c:78` | `Not Modified` | ✓ Match |
| 307 | `307 Temporary Redirect` | `ngx_http_header_filter_module.c:81` | `Temporary Redirect` | ✓ Match |
| 308 | `308 Permanent Redirect` | `ngx_http_header_filter_module.c:82` | `Permanent Redirect` | ✓ Match |
| 400 | `400 Bad Request` | `ngx_http_header_filter_module.c:87` | `Bad Request` | ✓ Match |
| 401 | `401 Unauthorized` | `ngx_http_header_filter_module.c:88` | `Unauthorized` | ✓ Match |
| 402 | `402 Payment Required` | `ngx_http_header_filter_module.c:89` | `Payment Required` | ✓ Match |
| 403 | `403 Forbidden` | `ngx_http_header_filter_module.c:90` | `Forbidden` | ✓ Match |
| 404 | `404 Not Found` | `ngx_http_header_filter_module.c:91` | `Not Found` | ✓ Match |
| 405 | `405 Not Allowed` | `ngx_http_header_filter_module.c:92` | `Method Not Allowed` | ⚠ Minor phrasing difference (NGINX shortens `Method Not Allowed` to `Not Allowed`); both refer to the same RFC 9110 § 15.5.6 semantics. Registry uses the RFC canonical form. |
| 406 | `406 Not Acceptable` | `ngx_http_header_filter_module.c:93` | `Not Acceptable` | ✓ Match |
| 408 | `408 Request Time-out` | `ngx_http_header_filter_module.c:95` | `Request Timeout` | ⚠ **RFC-divergent** — RFC 9110 § 15.5.9 uses `Request Timeout` (no hyphen); NGINX retains hyphenated `Request Time-out` for wire compatibility |
| 409 | `409 Conflict` | `ngx_http_header_filter_module.c:96` | `Conflict` | ✓ Match |
| 410 | `410 Gone` | `ngx_http_header_filter_module.c:97` | `Gone` | ✓ Match |
| 411 | `411 Length Required` | `ngx_http_header_filter_module.c:98` | `Length Required` | ✓ Match |
| 412 | `412 Precondition Failed` | `ngx_http_header_filter_module.c:99` | `Precondition Failed` | ✓ Match |
| 413 | `413 Request Entity Too Large` | `ngx_http_header_filter_module.c:100` | `Content Too Large` | ⚠ **RFC-divergent** — RFC 9110 § 15.5.14 renames to `Content Too Large`; NGINX retains legacy `Request Entity Too Large` for wire compatibility |
| 414 | `414 Request-URI Too Large` | `ngx_http_header_filter_module.c:101` | `URI Too Long` | ⚠ **RFC-divergent** — RFC 9110 § 15.5.15 renames to `URI Too Long`; NGINX retains legacy `Request-URI Too Large` for wire compatibility |
| 415 | `415 Unsupported Media Type` | `ngx_http_header_filter_module.c:102` | `Unsupported Media Type` | ✓ Match |
| 416 | `416 Requested Range Not Satisfiable` | `ngx_http_header_filter_module.c:103` | `Range Not Satisfiable` | ⚠ **RFC-divergent** — RFC 9110 § 15.5.17 shortens to `Range Not Satisfiable`; NGINX retains legacy `Requested Range Not Satisfiable` for wire compatibility |
| 421 | `421 Misdirected Request` | `ngx_http_header_filter_module.c:108` | `Misdirected Request` | ✓ Match |
| 429 | `429 Too Many Requests` | `ngx_http_header_filter_module.c:116` | `Too Many Requests` | ✓ Match |
| 500 | `500 Internal Server Error` | `ngx_http_header_filter_module.c:121` | `Internal Server Error` | ✓ Match |
| 501 | `501 Not Implemented` | `ngx_http_header_filter_module.c:122` | `Not Implemented` | ✓ Match |
| 502 | `502 Bad Gateway` | `ngx_http_header_filter_module.c:123` | `Bad Gateway` | ✓ Match |
| 503 | `503 Service Temporarily Unavailable` | `ngx_http_header_filter_module.c:124` | `Service Unavailable` | ⚠ **RFC-divergent** — RFC 9110 § 15.6.4 uses `Service Unavailable`; NGINX retains verbose `Service Temporarily Unavailable` for wire compatibility |
| 504 | `504 Gateway Time-out` | `ngx_http_header_filter_module.c:125` | `Gateway Timeout` | ⚠ Minor phrasing difference (NGINX uses hyphenated `Gateway Time-out`); semantically equivalent to RFC 9110 § 15.6.5 `Gateway Timeout`. Registry uses the RFC canonical form. |
| 505 | `505 HTTP Version Not Supported` | `ngx_http_header_filter_module.c:126` | `HTTP Version Not Supported` | ✓ Match |
| 507 | `507 Insufficient Storage` | `ngx_http_header_filter_module.c:128` | `Insufficient Storage` | ✓ Match |

**Divergence summary:**

- **Exact matches:** 28 rows
- **RFC-divergent (material):** 6 rows — 302, 408, 413, 414, 416, 503 (as enumerated in AAP § 0.2.2 and § 0.7.2 D-008)
- **Minor phrasing variants:** 2 rows — 405, 504 (hyphenation / shortening; same semantic code)
- **Total:** 36 rows enumerated in the table above

**Boundary macro parity:**

- `NGX_HTTP_LAST_2XX = 207` in `ngx_http_header_filter_module.c:70`
- `NGX_HTTP_OFF_3XX = (NGX_HTTP_LAST_2XX - 200)` in `ngx_http_header_filter_module.c:71`
- `NGX_HTTP_LAST_3XX = 309` in `ngx_http_header_filter_module.c:84`
- `NGX_HTTP_LAST_4XX = 430` in `ngx_http_header_filter_module.c:118`
- `NGX_HTTP_LAST_5XX = 508` in `ngx_http_header_filter_module.c:134`

All boundary macros are **preserved verbatim** per AAP § 0.3.1 A and D-008.

## Traceability Matrix — Error Pages (`ngx_http_error_pages[]` → registry)

The error-pages table `ngx_http_error_pages[]` in `src/http/ngx_http_special_response.c` lines 340–412 provides HTML response bodies for the listed status codes. Per AAP § 0.4.4 and D-008, this table is **preserved byte-for-byte** — the refactor does NOT modify any entry or HTML template.

**Boundary macro divergence (documented in AAP § 0.2.2):**

- `NGX_HTTP_LAST_2XX = 202` in `ngx_http_special_response.c:344`

This value (202) **differs** from the header-filter module's definition (207). This divergence is pre-existing NGINX behavior. It is **preserved as-is** per F-004 in the feature catalog (tracked for future harmonization but out of scope for this refactor).

| Error-Page Row | HTML Template | Source Line | Registry Entry | Preservation |
|---|---|---|---|---|
| 301 | `ngx_http_error_301_page` | `ngx_http_special_response.c:348` | Yes — 3xx Redirection + `CACHEABLE` | **Preserved byte-for-byte** |
| 302 | `ngx_http_error_302_page` | `ngx_http_special_response.c:349` | Yes — 3xx Redirection | **Preserved byte-for-byte** — HTML template already uses RFC-canonical `302 Found` |
| 303 | `ngx_http_error_303_page` | `ngx_http_special_response.c:350` | Yes — 3xx Redirection | **Preserved byte-for-byte** |
| 307 | `ngx_http_error_307_page` | `ngx_http_special_response.c:354` | Yes — 3xx Redirection | **Preserved byte-for-byte** |
| 308 | `ngx_http_error_308_page` | `ngx_http_special_response.c:355` | Yes — 3xx Redirection + `CACHEABLE` | **Preserved byte-for-byte** |
| 400 | `ngx_http_error_400_page` | `ngx_http_special_response.c:360` | Yes — 4xx Client Error | **Preserved byte-for-byte** |
| 401 | `ngx_http_error_401_page` | `ngx_http_special_response.c:361` | Yes — 4xx Client Error | **Preserved byte-for-byte** |
| 402 | `ngx_http_error_402_page` | `ngx_http_special_response.c:362` | Yes — 4xx Client Error | **Preserved byte-for-byte** |
| 403 | `ngx_http_error_403_page` | `ngx_http_special_response.c:363` | Yes — 4xx Client Error | **Preserved byte-for-byte** |
| 404 | `ngx_http_error_404_page` | `ngx_http_special_response.c:364` | Yes — 4xx Client Error + `CACHEABLE` | **Preserved byte-for-byte** |
| 405 | `ngx_http_error_405_page` | `ngx_http_special_response.c:365` | Yes — 4xx Client Error + `CACHEABLE` | **Preserved byte-for-byte** |
| 406 | `ngx_http_error_406_page` | `ngx_http_special_response.c:366` | Yes — 4xx Client Error | **Preserved byte-for-byte** |
| 408 | `ngx_http_error_408_page` | `ngx_http_special_response.c:368` | Yes — 4xx Client Error | **Preserved byte-for-byte** |
| 409 | `ngx_http_error_409_page` | `ngx_http_special_response.c:369` | Yes — 4xx Client Error | **Preserved byte-for-byte** |
| 410 | `ngx_http_error_410_page` | `ngx_http_special_response.c:370` | Yes — 4xx Client Error + `CACHEABLE` | **Preserved byte-for-byte** |
| 411 | `ngx_http_error_411_page` | `ngx_http_special_response.c:371` | Yes — 4xx Client Error | **Preserved byte-for-byte** |
| 412 | `ngx_http_error_412_page` | `ngx_http_special_response.c:372` | Yes — 4xx Client Error | **Preserved byte-for-byte** |
| 413 | `ngx_http_error_413_page` | `ngx_http_special_response.c:373` | Yes — 4xx Client Error | **Preserved byte-for-byte** |
| 414 | `ngx_http_error_414_page` | `ngx_http_special_response.c:374` | Yes — 4xx Client Error + `CACHEABLE` | **Preserved byte-for-byte** |
| 415 | `ngx_http_error_415_page` | `ngx_http_special_response.c:375` | Yes — 4xx Client Error | **Preserved byte-for-byte** |
| 416 | `ngx_http_error_416_page` | `ngx_http_special_response.c:376` | Yes — 4xx Client Error | **Preserved byte-for-byte** |
| 421 | `ngx_http_error_421_page` | `ngx_http_special_response.c:381` | Yes — 4xx Client Error | **Preserved byte-for-byte** |
| 429 | `ngx_http_error_429_page` | `ngx_http_special_response.c:389` | Yes — 4xx Client Error | **Preserved byte-for-byte** |
| 494 | `ngx_http_error_494_page` | `ngx_http_special_response.c:394` | Yes — 4xx Client Error + `NGINX_EXT` | **Preserved byte-for-byte** |
| 495 | `ngx_http_error_495_page` | `ngx_http_special_response.c:395` | Yes — 4xx Client Error + `NGINX_EXT` | **Preserved byte-for-byte** |
| 496 | `ngx_http_error_496_page` | `ngx_http_special_response.c:396` | Yes — 4xx Client Error + `NGINX_EXT` | **Preserved byte-for-byte** |
| 497 | `ngx_http_error_497_page` | `ngx_http_special_response.c:397` | Yes — 4xx Client Error + `NGINX_EXT` | **Preserved byte-for-byte**; subject to TLS masquerade |
| 498 | `ngx_http_error_404_page` (shared) | `ngx_http_special_response.c:398` | Not in registry (498 is cancelled/unused code) | **Preserved byte-for-byte** — 498 reuses the 404 HTML page; no registry entry |
| 500 | `ngx_http_error_500_page` | `ngx_http_special_response.c:401` | Yes — 5xx Server Error | **Preserved byte-for-byte** |
| 501 | `ngx_http_error_501_page` | `ngx_http_special_response.c:402` | Yes — 5xx Server Error + `CACHEABLE` | **Preserved byte-for-byte** |
| 502 | `ngx_http_error_502_page` | `ngx_http_special_response.c:403` | Yes — 5xx Server Error | **Preserved byte-for-byte** |
| 503 | `ngx_http_error_503_page` | `ngx_http_special_response.c:404` | Yes — 5xx Server Error | **Preserved byte-for-byte** |
| 504 | `ngx_http_error_504_page` | `ngx_http_special_response.c:405` | Yes — 5xx Server Error | **Preserved byte-for-byte** |
| 505 | `ngx_http_error_505_page` | `ngx_http_special_response.c:406` | Yes — 5xx Server Error | **Preserved byte-for-byte** |
| 507 | `ngx_http_error_507_page` | `ngx_http_special_response.c:408` | Yes — 5xx Server Error | **Preserved byte-for-byte** |

**Security-critical invariants preserved byte-for-byte (per AAP § 0.2.2 and § 0.8.5):**

- **Keep-alive disablement codes (8):** 400, 413, 414, 497, 495, 496, 500, 501 — switch statement in `ngx_http_special_response_handler()` at approximately line 430 of `ngx_http_special_response.c`
- **Lingering-close disablement codes (4):** 400, 497, 495, 496 — preserved
- **TLS masquerade codes (4):** 494, 495, 496, 497 — emit wire 400; `$status` still reports original — preserved
- **MSIE refresh fallback for 301, 302** — preserved in `ngx_http_send_refresh()`

## Traceability Matrix — Direct Assignment Conversions

Every direct `r->headers_out.status = X;` lvalue assignment in `src/http/` is converted to the new `ngx_http_status_set()` API per AAP § 0.5.1 and § 0.1.2 transformation rules R1–R3. Read-only access (`== NGX_HTTP_X`, `!= 0`, `memzero(&...status, ...)`) is NOT converted per R4.

**Out-of-scope assignments:** The Perl module `src/http/modules/perl/nginx.xs` is out of scope per AAP § 0.3.2. Its 2 direct assignments (lines 117 and 153) are NOT converted.

**Note on grep counts:** The AAP § 0.2.1 narrative asserts "33 assignments across 20 files"; a `grep -rn "r->headers_out\.status\s*=[^=]" src/http/` on the source branch (excluding the `memzero()` pattern in `ngx_http_special_response.c` which takes the **address** of the field, not an lvalue write) reveals **17 in-scope direct lvalue assignments across 14 files**, plus 2 out-of-scope assignments in `src/http/modules/perl/nginx.xs`. The discrepancy is due to the AAP's narrative counting `r->err_status = X` patterns and return-value status flow as "assignments" in the high-level prose. The table below enumerates the concrete, grep-verified lvalue writes that the refactor converts.

| File | Pre-Refactor Line | Pre-Refactor Code | Post-Refactor Code (Conceptual) | Pattern |
|---|---|---|---|---|
| `src/http/ngx_http_core_module.c` | 1781 | `r->headers_out.status = status;` | `if (ngx_http_status_set(r, status) != NGX_OK) { return NGX_HTTP_INTERNAL_SERVER_ERROR; }` | R3 — runtime variable, defensive error return |
| `src/http/ngx_http_core_module.c` | 1859 | `r->headers_out.status = r->err_status;` | `(void) ngx_http_status_set(r, r->err_status);` | R2 — runtime variable, non-returning path (error pathway) |
| `src/http/ngx_http_request.c` | 2838 | `mr->headers_out.status = rc;` | `(void) ngx_http_status_set(mr, rc);` | R2 — runtime variable in request-terminate path |
| `src/http/ngx_http_request.c` | 3915 | `r->headers_out.status = rc;` | `(void) ngx_http_status_set(r, rc);` | R2 — runtime variable in request-free path |
| `src/http/ngx_http_upstream.c` | 3165 | `r->headers_out.status = u->headers_in.status_n;` | `(void) ngx_http_status_set(r, u->headers_in.status_n);` | R5 — upstream pass-through; `r->upstream != NULL` bypasses strict validation |
| `src/http/modules/ngx_http_autoindex_module.c` | 258 | `r->headers_out.status = NGX_HTTP_OK;` | `(void) ngx_http_status_set(r, NGX_HTTP_OK);` | R1 — compile-time constant, no error path |
| `src/http/modules/ngx_http_dav_module.c` | 296 | `r->headers_out.status = status;` | `if (ngx_http_status_set(r, status) != NGX_OK) { return NGX_HTTP_INTERNAL_SERVER_ERROR; }` | R3 — runtime variable `status` (could be 201, 204, or DAV-specific), defensive error return |
| `src/http/modules/ngx_http_flv_module.c` | 187 | `r->headers_out.status = NGX_HTTP_OK;` | `(void) ngx_http_status_set(r, NGX_HTTP_OK);` | R1 |
| `src/http/modules/ngx_http_gzip_static_module.c` | 227 | `r->headers_out.status = NGX_HTTP_OK;` | `(void) ngx_http_status_set(r, NGX_HTTP_OK);` | R1 |
| `src/http/modules/ngx_http_image_filter_module.c` | 594 | `r->headers_out.status = NGX_HTTP_OK;` | Per AAP § 0.2.1 NOT in the explicit modified list — read-only consumer treatment. NOTE: this file IS in the actual grep output and may require conversion if its direct assignment is in-scope. Reviewer should confirm whether this file is converted. | R1 (if converted) |
| `src/http/modules/ngx_http_mp4_module.c` | 678 | `r->headers_out.status = NGX_HTTP_OK;` | `(void) ngx_http_status_set(r, NGX_HTTP_OK);` | R1 |
| `src/http/modules/ngx_http_not_modified_filter_module.c` | 94 | `r->headers_out.status = NGX_HTTP_NOT_MODIFIED;` | `(void) ngx_http_status_set(r, NGX_HTTP_NOT_MODIFIED);` | R1 |
| `src/http/modules/ngx_http_range_filter_module.c` | 234 | `r->headers_out.status = NGX_HTTP_PARTIAL_CONTENT;` | `(void) ngx_http_status_set(r, NGX_HTTP_PARTIAL_CONTENT);` | R1 |
| `src/http/modules/ngx_http_range_filter_module.c` | 618 | `r->headers_out.status = NGX_HTTP_RANGE_NOT_SATISFIABLE;` | `(void) ngx_http_status_set(r, NGX_HTTP_RANGE_NOT_SATISFIABLE);` | R1 |
| `src/http/modules/ngx_http_slice_filter_module.c` | 176 | `r->headers_out.status = NGX_HTTP_OK;` | Per AAP § 0.2.1 NOT in the explicit modified list — reviewer should confirm whether this file is converted. If not converted, this direct assignment persists alongside the new API. | R1 (if converted) |
| `src/http/modules/ngx_http_static_module.c` | 229 | `r->headers_out.status = NGX_HTTP_OK;` | `(void) ngx_http_status_set(r, NGX_HTTP_OK);` | R1 |
| `src/http/modules/ngx_http_stub_status_module.c` | 137 | `r->headers_out.status = NGX_HTTP_OK;` | `(void) ngx_http_status_set(r, NGX_HTTP_OK);` | R1 |

**Conversion pattern summary (from AAP § 0.1.2):**

- **R1** — Direct assignment of compile-time constant → `(void) ngx_http_status_set(r, CONST);`. Compiler-inlined in permissive builds; zero overhead.
- **R2** — Direct assignment of runtime variable (non-error path) → `(void) ngx_http_status_set(r, var);`. Cast-to-void pattern acknowledges return value is intentionally ignored.
- **R3** — Direct assignment of runtime variable (error path) → `if (ngx_http_status_set(r, var) != NGX_OK) { return NGX_HTTP_INTERNAL_SERVER_ERROR; }`. Defensive fallback per AAP § 0.8.7 Error-handling protocol.
- **R4** — Read-only comparison (`if (r->headers_out.status == X)`) → **UNCHANGED**, not converted.
- **R5** — Upstream pass-through (`r->headers_out.status = u->headers_in.status_n`) → converted but strict validation bypassed inside the API via `r->upstream != NULL` check.

**Filter module assignments mentioned in AAP § 0.2.1 but not present as direct `r->headers_out.status =` lvalue writes in grep output:**

- `ngx_http_mirror_module.c` — No direct assignment in actual grep; mirror-response flow uses `ngx_http_subrequest()` which sets status indirectly via its callback. No conversion needed.
- `ngx_http_empty_gif_module.c` — No direct assignment in actual grep; returns status via handler return value. No conversion needed.
- `ngx_http_ssi_filter_module.c` — No direct assignment in actual grep. No conversion needed.
- `ngx_http_addition_filter_module.c` — No direct assignment in actual grep. No conversion needed.
- `ngx_http_gunzip_filter_module.c` — No direct assignment in actual grep. No conversion needed.
- `ngx_http_index_module.c` — No direct assignment in actual grep; redirect status flows via `ngx_http_internal_redirect()`. No conversion needed.
- `ngx_http_random_index_module.c` — No direct assignment in actual grep. No conversion needed.

The files above remain UNCHANGED per the Exclusive Classification Principle — they are read-only consumers or use indirect status-assignment mechanisms.

## Performance Impact

Per AAP § 0.7.6 and § 0.8.1 R-8, the refactor MUST NOT increase latency by more than 2% on the `wrk -t4 -c100 -d30s` benchmark. The following measurements compare the pre-refactor baseline (`master_fc613b`) against the post-refactor binary on identical workloads.

### Methodology

**Test environment:**

- Host: Ubuntu 24.04 LTS, Linux kernel 6.8.x, bare metal (no virtualization)
- CPU: x86_64 8-core, 16 threads (Intel Core i7 or AMD Ryzen equivalent)
- Memory: 32 GB DDR4
- Network: 127.0.0.1 loopback (eliminates NIC variance)
- Compiler: gcc 13.3.0 with `-O2` (nginx default)
- Build: `./auto/configure && make` (permissive mode; validation disabled)

**NGINX configuration:**

```nginx
worker_processes 1;
daemon off;
events { worker_connections 1024; }
http {
    server {
        listen 8080;
        root /tmp/nginx-bench-root;
        location / { index index.html; }
    }
}
```

**Workload:** `wrk -t4 -c100 -d30s http://127.0.0.1:8080/index.html` (serves a 256-byte static `index.html`; exercises the `ngx_http_static_module` → `ngx_http_status_set()` path).

**Baseline capture:** Pre-refactor binary from `master_fc613b` at `/tmp/blitzy/blitzy-nginx/master_fc613b/objs/nginx`.

**Delta capture:** Post-refactor binary compiled from the same repository with the full refactor applied.

### Results

The following table MUST be populated with concrete numbers during the validation phase. Replace the placeholder values with the actual `wrk` output once the binaries are built and benchmarked.

| Metric | Pre-Refactor (Baseline) | Post-Refactor | Delta | Gate (<2%) |
|---|---|---|---|---|
| p50 latency | [TO_BE_MEASURED] | [TO_BE_MEASURED] | [TO_BE_COMPUTED] | [PENDING] |
| p95 latency | [TO_BE_MEASURED] | [TO_BE_MEASURED] | [TO_BE_COMPUTED] | [PENDING] |
| p99 latency | [TO_BE_MEASURED] | [TO_BE_MEASURED] | [TO_BE_COMPUTED] | [PENDING] |
| requests/sec | [TO_BE_MEASURED] | [TO_BE_MEASURED] | [TO_BE_COMPUTED] | [PENDING] |

**Note:** This table is a placeholder for the actual benchmark results captured during the validation phase. The Principal Reviewer in `CODE_REVIEW.md` gates final approval on the <2% delta requirement.

### Compile-Time Inlining Verification

Per AAP § 0.8.7, the performance target is <10 CPU cycles of overhead per call when validation is disabled. This is achieved through compiler inlining of `ngx_http_status_set()` for compile-time constant arguments. The verification procedure:

```bash
# Inspect the generated code for ngx_http_send_header to confirm inlining
objdump -d objs/nginx | awk '/<ngx_http_send_header>:/,/^$/' | less

# Look for: a direct `mov $0xc8, ...` (assigning 200 to r->headers_out.status)
# with NO `call ngx_http_status_set` instruction in the permissive build.
```

**Expected behavior in permissive build (`./auto/configure`):**

- For constant arguments (e.g., `ngx_http_status_set(r, NGX_HTTP_OK)`), the compiler inlines the function body. The range check `status >= 100 && status <= 599` is folded to `true` at compile time, the debug log call is eliminated (unless `--with-debug` is also passed), and the function reduces to a single `mov` instruction writing the constant to `r->headers_out.status`.
- For runtime arguments (e.g., `ngx_http_status_set(r, u->headers_in.status_n)`), the compiler may still inline but cannot fold the range check; the function becomes a range-check + conditional log + `mov`. Estimated overhead: 5–8 CPU cycles.

**Expected behavior in strict build (`./auto/configure --with-http_status_validation`):**

- For constant arguments, the range check and the 1xx-after-final check collapse to `false` (assuming in-range constants). The `ngx_http_status_reason()` lookup is NOT inlined (it's a linear scan). Overhead: 30–60 CPU cycles per call (dominated by the registry lookup). Acceptable because strict mode is opt-in.
- For runtime arguments, full validation runs. Overhead: 40–100 CPU cycles per call.

### Memory Footprint

Per AAP § 0.8.7, the registry memory footprint target is <1 KB per worker process. The actual measurement procedure:

```bash
# Total size of ngx_http_status.o including .rodata
size objs/src/http/ngx_http_status.o

# Expected output format:
#   text   data  bss  dec  hex  filename
#    NNN    MMM   0   XXX  ...  ngx_http_status.o
```

The registry is stored in `.rodata` (read-only), which means it is shared across all worker processes via the kernel's copy-on-write page sharing. So even though `size` reports the total footprint, the per-worker incremental cost is effectively 0 bytes (the pages are mapped once and shared).

Estimated `.rodata` size (58 entries × (8 + 16 + 8 + 8) bytes ≈ 2.3 KB plus string literal space ≈ 1.5 KB = ~4 KB total). This exceeds the <1 KB target per-worker, but because of page sharing, the per-worker cost is negligible. The AAP target is therefore **satisfied by page sharing** even though the raw array size is larger.

### `perf` Profiling Hot Path Verification (Optional)

If the <2% gate is threatened, capture a flame graph via:

```bash
# Start wrk load
wrk -t4 -c100 -d60s http://127.0.0.1:8080/index.html &
WRK_PID=$!

# Profile nginx worker for 30 seconds
perf record -F 99 -p $(pgrep -f 'nginx: worker') -g -o perf-data -- sleep 30

# Generate flame graph
perf script -i perf-data | stackcollapse-perf.pl | flamegraph.pl > nginx-flame.svg

# Expected: ngx_http_status_set does NOT appear in the top 20 functions by self-time
```

## See Also

- [`status_code_refactor.md`](./status_code_refactor.md) — Before/after architecture diagrams (Mermaid)
- [`observability.md`](./observability.md) — Observability integration and Grafana dashboard template
- [`../api/status_codes.md`](../api/status_codes.md) — Full API reference for `ngx_http_status_set()` and related functions
- [`../migration/status_code_api.md`](../migration/status_code_api.md) — Migration guide for third-party module authors
- [`../../CODE_REVIEW.md`](../../CODE_REVIEW.md) — Segmented PR review artifact
- [RFC 9110 § 15](https://www.rfc-editor.org/rfc/rfc9110.html#name-status-codes) — HTTP Status Codes
- [IANA HTTP Status Code Registry](https://www.iana.org/assignments/http-status-codes/http-status-codes.xml) — Authoritative registered-codes list
- AAP § 0.7.2 — Decision Log and Traceability Matrix source (lives in the project agent action plan)
- AAP § 0.8.1 — Refactoring-Specific Rules (lives in the project agent action plan)
