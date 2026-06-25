# Status-Code Refactor: Traceability Matrix

This matrix provides **bidirectional traceability** between the legacy
status-handling constructs of nginx and the new centralized registry/facade
implementation, at **100% coverage with no gaps**. For every legacy
construct — each `NGX_HTTP_*` numeric constant, each scattered "Pattern A"
direct field write (`r->headers_out.status = ...`), and each read-side
comparison — this document records its corresponding target: a registry entry,
an `ngx_http_status_set()` call, or an explicit, justified "preserved /
out-of-scope" decision.

The two halves of the mapping are:

- **Source → Target.** Every `NGX_HTTP_*` constant maps to a row in the
  immutable `static const ngx_http_status_def_t[]` registry (Table A), and
  every Pattern A write-site maps to an `ngx_http_status_set(r, <code>)` call
  (Table B).
- **Target → Source.** Every registry entry and API call site is anchored back
  to the exact file and line it occupies in the committed implementation
  (Tables B and C), so a reviewer can navigate in either direction.

The registry (`static const ngx_http_status_def_t[]`) and the five facade
functions — `ngx_http_status_set`, `ngx_http_status_validate`,
`ngx_http_status_reason`, `ngx_http_status_register`, and
`ngx_http_status_is_cacheable` — live in
`src/http/ngx_http_request.c`. The `NGX_HTTP_*` numeric
constants and the `NGX_HTTP_STATUS_*` classification-flag macros, together with
the `ngx_http_status_def_t` record type, live in
`src/http/ngx_http_request.h`.

> **Related:** see the [Migration Guide](status_code_api.md) for API signatures,
> before/after conversion patterns, backward-compatibility guarantees, and the
> third-party deprecation roadmap. This matrix is intentionally complementary —
> it enumerates *coverage* and does not duplicate the guide's prose.

> **Verification anchor.** Every line number in this matrix was verified against
> the status-code refactor as committed on this branch (HEAD `f5d6ee19e`). Line
> numbers point at the `ngx_http_status_set()` call (write-sites) or the
> field read (read-side comparisons) in the committed source, so each reference
> resolves to a real location in the shippable code.

## Classification Flags

The registry's `flags` field OR-combines four classification bits defined in
`src/http/ngx_http_request.h` and consumed by `ngx_http_status_is_cacheable()`
and the registry-lookup helpers in `src/http/ngx_http_request.c`:

| Flag macro | Value | Meaning |
|---|---|---|
| `NGX_HTTP_STATUS_INFORMATIONAL` | `0x0008` | 1xx informational responses (RFC 9110 §15.2). |
| `NGX_HTTP_STATUS_CACHEABLE` | `0x0001` | Heuristically cacheable by default (RFC 9110 §15.1 / RFC 9111). |
| `NGX_HTTP_STATUS_CLIENT_ERROR` | `0x0002` | 4xx client-error responses (RFC 9110 §15.5). |
| `NGX_HTTP_STATUS_SERVER_ERROR` | `0x0004` | 5xx server-error responses (RFC 9110 §15.6). |

The **canonical `NGX_HTTP_STATUS_CACHEABLE` set is exactly 12 codes**:
**200, 203, 204, 206, 300, 301, 308, 404, 405, 410, 414, 501.** This mirrors the
RFC 9110 §15.1 / RFC 9111 heuristically-cacheable subset adopted by the refactor.

## Table A — Every `NGX_HTTP_*` Constant → Registry Entry

Each `NGX_HTTP_*` numeric constant defined in `src/http/ngx_http_request.h`
(L74–L145) maps to one entry in the immutable `static const
ngx_http_status_def_t status_registry[]` table in `src/http/ngx_http_request.c`.
The **Class flag** column lists the classification bit the registry assigns to
that entry; the **Cacheable** column marks (`✔`) the 12 codes in the canonical
`NGX_HTTP_STATUS_CACHEABLE` set. Two codes — **203** and **410** — are
**registry-only**: they carry no `NGX_HTTP_*` constant in `ngx_http_request.h`
yet exist as cacheable registry entries.

### 1xx — Informational (`NGX_HTTP_STATUS_INFORMATIONAL`)

| Constant | Code | Class flag | Cacheable | Notes |
|---|---|---|---|---|
| `NGX_HTTP_CONTINUE` | 100 | `NGX_HTTP_STATUS_INFORMATIONAL` | — | |
| `NGX_HTTP_SWITCHING_PROTOCOLS` | 101 | `NGX_HTTP_STATUS_INFORMATIONAL` | — | |
| `NGX_HTTP_PROCESSING` | 102 | `NGX_HTTP_STATUS_INFORMATIONAL` | — | |
| `NGX_HTTP_EARLY_HINTS` | 103 | `NGX_HTTP_STATUS_INFORMATIONAL` | — | |

### 2xx — Successful

| Constant | Code | Class flag | Cacheable | Notes |
|---|---|---|---|---|
| `NGX_HTTP_OK` | 200 | — | ✔ | |
| `NGX_HTTP_CREATED` | 201 | — | — | |
| `NGX_HTTP_ACCEPTED` | 202 | — | — | |
| `(no NGX_HTTP_* constant)` | 203 | — | ✔ | **registry-only** — Non-Authoritative Information; no nginx constant but is a cacheable registry entry |
| `NGX_HTTP_NO_CONTENT` | 204 | — | ✔ | |
| `NGX_HTTP_PARTIAL_CONTENT` | 206 | — | ✔ | |

### 3xx — Redirection

| Constant | Code | Class flag | Cacheable | Notes |
|---|---|---|---|---|
| `NGX_HTTP_SPECIAL_RESPONSE` | 300 | — | ✔ | |
| `NGX_HTTP_MOVED_PERMANENTLY` | 301 | — | ✔ | |
| `NGX_HTTP_MOVED_TEMPORARILY` | 302 | — | — | |
| `NGX_HTTP_SEE_OTHER` | 303 | — | — | |
| `NGX_HTTP_NOT_MODIFIED` | 304 | — | — | |
| `NGX_HTTP_TEMPORARY_REDIRECT` | 307 | — | — | |
| `NGX_HTTP_PERMANENT_REDIRECT` | 308 | — | ✔ | |

### 4xx — Client Error (`NGX_HTTP_STATUS_CLIENT_ERROR`)

| Constant | Code | Class flag | Cacheable | Notes |
|---|---|---|---|---|
| `NGX_HTTP_BAD_REQUEST` | 400 | `NGX_HTTP_STATUS_CLIENT_ERROR` | — | |
| `NGX_HTTP_UNAUTHORIZED` | 401 | `NGX_HTTP_STATUS_CLIENT_ERROR` | — | |
| `NGX_HTTP_FORBIDDEN` | 403 | `NGX_HTTP_STATUS_CLIENT_ERROR` | — | |
| `NGX_HTTP_NOT_FOUND` | 404 | `NGX_HTTP_STATUS_CLIENT_ERROR` | ✔ | |
| `NGX_HTTP_NOT_ALLOWED` | 405 | `NGX_HTTP_STATUS_CLIENT_ERROR` | ✔ | |
| `NGX_HTTP_REQUEST_TIME_OUT` | 408 | `NGX_HTTP_STATUS_CLIENT_ERROR` | — | |
| `NGX_HTTP_CONFLICT` | 409 | `NGX_HTTP_STATUS_CLIENT_ERROR` | — | |
| `(no NGX_HTTP_* constant)` | 410 | `NGX_HTTP_STATUS_CLIENT_ERROR` | ✔ | **registry-only** — Gone; no nginx constant but is a cacheable registry entry |
| `NGX_HTTP_LENGTH_REQUIRED` | 411 | `NGX_HTTP_STATUS_CLIENT_ERROR` | — | |
| `NGX_HTTP_PRECONDITION_FAILED` | 412 | `NGX_HTTP_STATUS_CLIENT_ERROR` | — | |
| `NGX_HTTP_REQUEST_ENTITY_TOO_LARGE` | 413 | `NGX_HTTP_STATUS_CLIENT_ERROR` | — | |
| `NGX_HTTP_REQUEST_URI_TOO_LARGE` | 414 | `NGX_HTTP_STATUS_CLIENT_ERROR` | ✔ | |
| `NGX_HTTP_UNSUPPORTED_MEDIA_TYPE` | 415 | `NGX_HTTP_STATUS_CLIENT_ERROR` | — | |
| `NGX_HTTP_RANGE_NOT_SATISFIABLE` | 416 | `NGX_HTTP_STATUS_CLIENT_ERROR` | — | |
| `NGX_HTTP_MISDIRECTED_REQUEST` | 421 | `NGX_HTTP_STATUS_CLIENT_ERROR` | — | |
| `NGX_HTTP_TOO_MANY_REQUESTS` | 429 | `NGX_HTTP_STATUS_CLIENT_ERROR` | — | |

### Internal Sentinel Codes (nginx-only — whitelisted by `ngx_http_status_validate()`, never wire-emitted)

These codes fall numerically inside the 100–599 range but are nginx-internal
sentinels that trigger special connection handling rather than an emitted HTTP
status line. `ngx_http_status_validate()` whitelists / short-circuits this range
rather than rejecting it, so `ngx_http_status_set()` accepts them unchanged.

| Constant | Code | Class flag | Cacheable | Notes |
|---|---|---|---|---|
| `NGX_HTTP_CLOSE` | 444 | internal (whitelisted) | — | Special: close connection with no response |
| `NGX_HTTP_NGINX_CODES` | 494 | internal (whitelisted) | — | Base of the nginx-internal range |
| `NGX_HTTP_REQUEST_HEADER_TOO_LARGE` | 494 | internal (whitelisted) | — | **Same numeric value as `NGX_HTTP_NGINX_CODES`** (both = 494) |
| `NGX_HTTPS_CERT_ERROR` | 495 | internal (whitelisted) | — | Note: `NGX_HTTPS_` prefix (not `NGX_HTTP_`) |
| `NGX_HTTPS_NO_CERT` | 496 | internal (whitelisted) | — | Note: `NGX_HTTPS_` prefix |
| `NGX_HTTP_TO_HTTPS` | 497 | internal (whitelisted) | — | Plain HTTP request sent to an HTTPS port |
| `(498)` | 498 | internal (whitelisted) | — | Canceled code (invalid host name) — no constant |
| `NGX_HTTP_CLIENT_CLOSED_REQUEST` | 499 | internal (whitelisted) | — | Client closed the connection before the header was sent |

### 5xx — Server Error (`NGX_HTTP_STATUS_SERVER_ERROR`)

| Constant | Code | Class flag | Cacheable | Notes |
|---|---|---|---|---|
| `NGX_HTTP_INTERNAL_SERVER_ERROR` | 500 | `NGX_HTTP_STATUS_SERVER_ERROR` | — | |
| `NGX_HTTP_NOT_IMPLEMENTED` | 501 | `NGX_HTTP_STATUS_SERVER_ERROR` | ✔ | |
| `NGX_HTTP_BAD_GATEWAY` | 502 | `NGX_HTTP_STATUS_SERVER_ERROR` | — | |
| `NGX_HTTP_SERVICE_UNAVAILABLE` | 503 | `NGX_HTTP_STATUS_SERVER_ERROR` | — | |
| `NGX_HTTP_GATEWAY_TIME_OUT` | 504 | `NGX_HTTP_STATUS_SERVER_ERROR` | — | |
| `NGX_HTTP_VERSION_NOT_SUPPORTED` | 505 | `NGX_HTTP_STATUS_SERVER_ERROR` | — | |
| `NGX_HTTP_INSUFFICIENT_STORAGE` | 507 | `NGX_HTTP_STATUS_SERVER_ERROR` | — | |

**Cacheable confirmation.** The `NGX_HTTP_STATUS_CACHEABLE` set above is exactly
the 12 codes **200, 203, 204, 206, 300, 301, 308, 404, 405, 410, 414, 501**.
Of these, **203 and 410 have no `NGX_HTTP_*` constant** in `ngx_http_request.h`
but exist as registry-only cacheable entries.

## Table B — Every Pattern A Write-Site → `ngx_http_status_set()`

"Pattern A" is the direct field mutation `r->headers_out.status = NGX_HTTP_*;`.
Every Pattern A write-site below has been converted to a
`ngx_http_status_set(r, <code>)` call whose failure branch matches the
enclosing function's return contract. All paths are under
`src/http/modules/`. Line numbers identify the `ngx_http_status_set()` call in
the committed source. See the [Migration Guide](status_code_api.md) Phase 3
table for the full before/after code of each conversion.

| Module | Line | Code | Conversion notes |
|---|---|---|---|
| `ngx_http_static_module.c` | 229 | 200 | `ngx_int_t` handler → failure `return NGX_HTTP_INTERNAL_SERVER_ERROR;` |
| `ngx_http_autoindex_module.c` | 258 | 200 | `ngx_int_t` handler |
| `ngx_http_slice_filter_module.c` | 176 | 200 | Preserves read-side `==` at L204 (not converted) |
| `ngx_http_stub_status_module.c` | 137 | 200 | `ngx_int_t` handler |
| `ngx_http_image_filter_module.c` | 594 | 200 | Returns `ngx_buf_t *` → failure `return NULL;`; preserves read-side `==` at L227 |
| `ngx_http_dav_module.c` | 296 | 201 / 204 | `void` handler → failure `ngx_http_finalize_request(...); return;`; sets the `status` variable (201 `NGX_HTTP_CREATED` / 204 `NGX_HTTP_NO_CONTENT`) |
| `ngx_http_range_filter_module.c` | 234 | 206 | Preserves the adjacent `status_line.len = 0;` |
| `ngx_http_range_filter_module.c` | 623 | 416 | `NGX_HTTP_RANGE_NOT_SATISFIABLE` (in `ngx_http_range_not_satisfiable()`) |
| `ngx_http_flv_module.c` | 187 | 200 | |
| `ngx_http_not_modified_filter_module.c` | 94 | 304 | Preserves 304 header-only normalization |
| `ngx_http_mp4_module.c` | 678 | 200 | |
| `ngx_http_gzip_static_module.c` | 227 | 200 | |

**Total: 12 Pattern A write-sites converted** (one module — `ngx_http_range_filter_module.c` — contributes two: the 206 success path and the 416 error path).

## Table C — Central Choke Points, Consumers & Read-Side Coverage

This section accounts for every remaining status-handling surface so that
coverage is provably complete. None of these are Pattern A write-sites, so none
require per-line `r->headers_out.status = ...` conversion; instead they either
materialize status **centrally**, **consume** the registry, or **read** the
field (and are therefore preserved unchanged).

### Central finalization choke points (`src/http/ngx_http_core_module.c`)

These two functions are where status set by **Pattern B** (`return NGX_HTTP_*`)
and **Pattern C** (the `return` directive) materializes centrally; both route
through `ngx_http_status_set()`.

| Function | Line | Flow | Notes |
|---|---|---|---|
| `ngx_http_send_response()` | 1781 | `status` parameter → `ngx_http_status_set(r, status)` → `r->headers_out.status` | Central setter for in-handler responses |
| `ngx_http_send_header()` | 1863 | `r->err_status` → `ngx_http_status_set(r, r->err_status)` → `r->headers_out.status` | Where Pattern B/C error statuses materialize |

### Reason / consumer integration

| File | Integration | Notes |
|---|---|---|
| `ngx_http_header_filter_module.c` | `ngx_http_status_reason()` (L165) | Delegates default reason-phrase resolution to the registry, replacing direct `ngx_http_status_lines[]` indexing |
| `ngx_http_special_response.c` | `ngx_http_status_reason()` (L442), `ngx_http_status_is_cacheable()` (L446) | Consulted at the `r->err_status` dispatch; **`error_page` parsing is preserved exactly** |

### Read-side `==` comparisons — PRESERVED, NOT converted

These sites **read** `r->headers_out.status` (they do not write it), so there is
no write to convert; converting them would be meaningless. They are preserved
verbatim.

| File | Line(s) | Codes compared | Notes |
|---|---|---|---|
| `ngx_http_charset_filter_module.c` | 497–498 | 301 / 302 | `MOVED_PERMANENTLY` / `MOVED_TEMPORARILY` |
| `ngx_http_chunked_filter_module.c` | 65–71 | 304 / 204 / `< 200` / CONNECT `< 300` | Body-less / informational guard |
| `ngx_http_xslt_filter_module.c` | 210 | 304 | `NOT_MODIFIED` |
| `ngx_http_image_filter_module.c` | 227 | 304 | `NOT_MODIFIED` (same module also has the 200 write-site at L594) |
| `ngx_http_slice_filter_module.c` | 204 | 206 | `PARTIAL_CONTENT` (same module also has the 200 write-site at L176) |

### Protocol read-side validation hooks (log-only, only under `NGX_HTTP_STATUS_VALIDATION`)

These remain read-side consumers of `r->headers_out.status`; no encoder logic
changes. The optional validation hook calls `ngx_http_status_validate()` and is
compiled out unless the binary is built with
`--with-cc-opt="-DNGX_HTTP_STATUS_VALIDATION"` (compile-time macro
`NGX_HTTP_STATUS_VALIDATION`).

| File | Line | Surface | Notes |
|---|---|---|---|
| `src/http/v2/ngx_http_v2_filter_module.c` | 167 | HPACK `:status` | `ngx_http_status_validate(...)` call at L167, guarded by `#if (NGX_HTTP_STATUS_VALIDATION)` at L166 |
| `src/http/v3/ngx_http_v3_filter_module.c` | 123 | QPACK `:status` | `ngx_http_status_validate(...)` call at L123, guarded by `#if (NGX_HTTP_STATUS_VALIDATION)` at L122 |

### Out-of-scope and pass-through surfaces

- **`src/http/modules/perl/nginx.xs` status writes (L117, L153): OUT OF SCOPE.**
  These are the embedded-Perl boundary (`r->headers_out.status = SvIV(ST(1));`
  at L117 and the `= NGX_HTTP_OK` default at L153) and are intentionally not
  converted.
- **Pattern B / Pattern C modules** — `ngx_http_access_module.c` (403),
  `ngx_http_auth_basic_module.c` (401/403), `ngx_http_index_module.c` (403/404),
  `ngx_http_referer_module.c`, `ngx_http_rewrite_module.c` (the `return`
  directive), and `ngx_http_limit_conn_module.c` / `ngx_http_limit_req_module.c`
  (503) — adopt the API **centrally** at the finalization choke points above.
  There is **no per-`return` rewrite**.
- **Upstream pass-through** — `ngx_http_proxy_module.c`,
  `ngx_http_fastcgi_module.c`, `ngx_http_scgi_module.c`,
  `ngx_http_uwsgi_module.c`, `ngx_http_grpc_module.c`, and
  `ngx_http_memcached_module.c` pass origin-assigned status through unchanged.
  The value is never transformed or rejected: `ngx_http_status_set()` checks
  `r->upstream` before strict validation.

## 100% Coverage Statement & Rationale

**Coverage is 100% with no gaps.** Every legacy status-handling construct is
accounted for: every `NGX_HTTP_*` constant maps to a registry entry (Table A);
every Pattern A write-site maps to an `ngx_http_status_set()` call (Table B);
and every remaining surface — central choke points, registry consumers,
read-side comparisons, protocol hooks, the embedded-Perl boundary, Pattern B/C
modules, and upstream pass-through — is explicitly enumerated (Table C). No
status-handling construct in the in-scope HTTP tree is left unmapped.

Each category that is **not** subject to Pattern A conversion is excluded for a
concrete, intentional reason:

- **Read-side `==` comparisons** (Table C) — these *read* the status field, they
  do not *write* it. There is no assignment to convert, so applying
  `ngx_http_status_set()` would be meaningless; they are preserved verbatim.
- **Pattern B (`return NGX_HTTP_*`) and Pattern C (the `return` directive)** —
  the returned/parsed status materializes at the central finalization choke
  points (`ngx_http_send_response()` L1781 and `ngx_http_send_header()` L1863),
  which already route through `ngx_http_status_set()`. Rewriting every `return`
  statement would be redundant and would add risk for zero behavioral benefit.
- **`perl/nginx.xs`** — the embedded-Perl interpreter boundary is out of scope;
  its status writes (L117, L153) are not converted.
- **Upstream modules** (proxy/fastcgi/scgi/uwsgi/grpc/memcached) — these must
  pass the origin-assigned status through **unchanged**, honoring the
  prohibition on any upstream status transformation. The setter's `r->upstream`
  guard ensures origin statuses bypass strict validation.

### Coverage summary

| Category | Count | Disposition |
|---|---|---|
| Pattern A write-sites | 12 | Converted to `ngx_http_status_set()` (Table B) |
| Read-side `==` comparisons | 5 | Preserved (read-only; Table C) |
| Central finalization choke points | 2 | Route through `ngx_http_status_set()` (Table C) |
| Registry consumer integrations | 2 | `reason()` / `is_cacheable()` (Table C) |
| Protocol read-side validation hooks | 2 | Log-only under `NGX_HTTP_STATUS_VALIDATION` (Table C) |
| `NGX_HTTP_*` constants | all | Represented in the registry (Table A) |
| `perl/nginx.xs` writes | 2 | Out of scope (embedded Perl) |
| Upstream pass-through modules | 6 | Origin status passed through unchanged |

