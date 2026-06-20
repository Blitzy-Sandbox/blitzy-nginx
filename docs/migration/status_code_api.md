# HTTP Status Code API — Migration Guide

This guide shows NGINX module authors how to migrate direct `r->headers_out.status`
writes to the centralized `ngx_http_status_set()` API introduced by the HTTP
status-code registry refactor. It explains the canonical conversion pattern, the two
build modes (the permissive default versus the strict `--with-http_status_validation`
build), the upstream/proxied pass-through exception, and the reason-phrase lookup
change.

The refactor is **structural only**. Wire output — both the status line and any
error-page body — is **byte-identical** before and after migration, and the
**default build adds zero overhead**: `ngx_http_status_set()` is a `static ngx_inline`
that compiles to exactly the field write it replaces.

## API at a glance

The API is declared in `src/http/ngx_http_status.h` and implemented in
`src/http/ngx_http_status.c`. It is exposed transitively through `ngx_http.h`, so
module authors do **not** add a direct `#include "ngx_http_status.h"`.

| Function | Signature | Purpose |
|---|---|---|
| `ngx_http_status_set` | `ngx_int_t ngx_http_status_set(ngx_http_request_t *r, ngx_uint_t status)` | The single status-assignment chokepoint; replaces every `r->headers_out.status = ...` write. |
| `ngx_http_status_validate` | `ngx_int_t ngx_http_status_validate(ngx_uint_t status)` | Returns `NGX_OK` when `status` is in the RFC 9110 range `100..599`, otherwise `NGX_ERROR`. |
| `ngx_http_status_reason` | `ngx_str_t ngx_http_status_reason(ngx_uint_t status)` | Returns the full status-line text (e.g. `"404 Not Found"`), or an empty string for numeric-only codes. |
| `ngx_http_status_register` | `ngx_int_t ngx_http_status_register(const ngx_http_status_def_t *def)` | Pre-fork extensibility seam for registering third-party codes. |
| `ngx_http_status_is_cacheable` | `ngx_uint_t ngx_http_status_is_cacheable(ngx_uint_t status)` | Returns `1` if the code carries the cacheable flag, otherwise `0` (metadata only). |

Each code is described by a single registry descriptor:

```c
typedef struct {
    ngx_uint_t   code;        /* e.g. 404 */
    ngx_str_t    reason;      /* full status-line text, or empty for numeric-only */
    ngx_uint_t   flags;       /* class + cacheability bitmask */
    const char  *rfc_section; /* "15.5.5" */
} ngx_http_status_def_t;
```

The `flags` field is a bitmask of the metadata bits:

```c
#define NGX_HTTP_STATUS_CACHEABLE       0x0001
#define NGX_HTTP_STATUS_CLIENT_ERROR    0x0002
#define NGX_HTTP_STATUS_SERVER_ERROR    0x0004
#define NGX_HTTP_STATUS_INFORMATIONAL   0x0008
```

## 1. Why migrate

Before the refactor, a response status was written directly to the
`r->headers_out.status` struct field from roughly 20 modules across 33 distinct
assignment sites. Because the write was a bare struct-field assignment, there was
**no place to validate a code, attach metadata, or instrument the assignment** —
the missing chokepoint identified as root cause **RC-2**.

`ngx_http_status_set()` introduces that single chokepoint. Every status assignment
now flows through one function, which makes it possible to add optional RFC 9110
validation, reason-phrase and metadata lookups, and future instrumentation
**without touching the call sites again**.

This is a purely structural refactor with **full backward compatibility**. The
default build behaves exactly as before — the setter inlines to the original field
write — and the emitted wire output is unchanged. Migration is therefore a
mechanical, low-risk transformation of the call sites.

## 2. Canonical conversion

The canonical pattern replaces the field write with a guarded call to the setter.
Reproduce it exactly, in NGINX style (the `ngx_` prefix, `snake_case`, and
4-space indentation):

```c
/* OLD */
r->headers_out.status = NGX_HTTP_NOT_FOUND;

/* NEW */
if (ngx_http_status_set(r, 404) != NGX_OK) {
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                  "invalid HTTP status %ui", (ngx_uint_t) 404);
    return NGX_HTTP_INTERNAL_SERVER_ERROR;
}
```

Three points govern this "hard return" form:

- The enclosing function **must return `ngx_int_t`** so that the
  `return NGX_HTTP_INTERNAL_SERVER_ERROR;` fallback is valid. Functions on a
  finalize, terminate, or free path use the permissive `(void)` form instead
  (see [Section 3](#3-permissive-vs-strict-mode)).
- In the **default (non-validation) build**, `ngx_http_status_set()` inlines to the
  original field write plus `return NGX_OK`, so the entire error branch
  **constant-folds away**. The result is zero behavior change and zero added
  overhead.
- You may pass either the numeric code (`404`) or an existing constant or variable.
  `NGX_HTTP_OK` constant-folds to `200`, and an `ngx_uint_t status` variable is
  passed through unchanged.

### Canonical migrated site: the static content handler

`src/http/modules/ngx_http_static_module.c` is the reference example. Its content
handler returns `ngx_int_t`, so the hard-return form applies directly. Note that the
adjacent field assignments are preserved exactly — only the status write changes:

```c
/* OLD (src/http/modules/ngx_http_static_module.c) */
r->headers_out.status = NGX_HTTP_OK;
r->headers_out.content_length_n = of.size;
r->headers_out.last_modified_time = of.mtime;

/* NEW (src/http/modules/ngx_http_static_module.c) */
if (ngx_http_status_set(r, 200) != NGX_OK) {
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                  "invalid HTTP status %ui", (ngx_uint_t) 200);
    return NGX_HTTP_INTERNAL_SERVER_ERROR;
}
r->headers_out.content_length_n = of.size;
r->headers_out.last_modified_time = of.mtime;
```

The migrated site passes the numeric literal `200`. Because `NGX_HTTP_OK` is defined
as `200`, `ngx_http_status_set(r, NGX_HTTP_OK)` is equivalent — the constant folds to
`200` at compile time. **Only the status write changes**; the surrounding
`content_length_n` and `last_modified_time` assignments stay exactly as they were.

### Passing an existing variable: the core module `err_status` path

When the code is carried by a variable rather than a literal, pass the variable
straight through. `src/http/ngx_http_core_module.c` writes `r->err_status` and must
preserve the adjacent `status_line.len = 0;` reset:

```c
/* OLD (src/http/ngx_http_core_module.c, err_status path) */
if (r->err_status) {
    r->headers_out.status = r->err_status;
    r->headers_out.status_line.len = 0;
}

/* NEW (src/http/ngx_http_core_module.c, err_status path) */
if (r->err_status) {
    if (ngx_http_status_set(r, r->err_status) != NGX_OK) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "invalid HTTP status %ui", (ngx_uint_t) r->err_status);
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
    r->headers_out.status_line.len = 0;
}
```

The nginx extension codes (444 and 494–499) are part of the registry and fall inside
the valid `100..599` range, so an `r->err_status` that carries an extension code
**still validates successfully** — there is no spurious `500` in the strict build.

## 3. Permissive vs strict mode

`ngx_http_status_set()` has two build-time personalities, selected by the
`--with-http_status_validation` configure flag.

- **Default build (no flag) — permissive, zero overhead.** `ngx_http_status_set()`
  is a `static ngx_inline` equivalent to the legacy `r->headers_out.status = status;`.
  It always returns `NGX_OK`, so the error branch of the canonical pattern compiles
  away entirely. There is no runtime cost and no behavior change.

- **`--with-http_status_validation` build — strict.** The flag defines
  `NGX_HTTP_STATUS_VALIDATION 1` and turns `ngx_http_status_set()` into a real
  function. For a locally generated response it calls `ngx_http_status_validate()`,
  which returns `NGX_OK` only when the code lies in the RFC 9110 `100..599` range.
  An **out-of-range** code makes the setter return `NGX_ERROR`. The setter itself
  does **not** emit an `NGX_LOG_ERR`; the **caller** logs the single
  `"invalid HTTP status %ui"` line and falls back to
  `NGX_HTTP_INTERNAL_SERVER_ERROR` — exactly the hard-return pattern from
  [Section 2](#2-canonical-conversion).

Because validation is a structural range check, every standard code and every nginx
extension code (444, 494–499) is accepted; only a code outside `100..599` is
rejected.

### The permissive `(void)` call form

On a finalize, terminate, or free path — where returning an HTTP error is not
meaningful — use the `(void)` form, which discards the return value:

```c
(void) ngx_http_status_set(r, rc);
```

`src/http/ngx_http_request.c` uses exactly this form on its request
terminate and free paths. In the default build the `(void)` call is identical to the
old field write. In the strict build it still validates and logs at debug level, but
the path **continues regardless** — the value is set either way, and no error is
returned up the stack.

## 4. Upstream / proxied pass-through exception

!!! warning "Proxied/upstream status is copied, never strict-validated"
    A status produced by an upstream (proxy, FastCGI, uwsgi, gRPC, …) is **copied
    through verbatim** and must never be strict-validated. Always use the permissive
    `(void)` form on these paths and never return an error for a proxied code.

This is a hard constraint of the refactor. `src/http/ngx_http_upstream.c` copies the
backend's status and the adjacent textual status line:

```c
/* OLD (src/http/ngx_http_upstream.c) */
r->headers_out.status = u->headers_in.status_n;
r->headers_out.status_line = u->headers_in.status_line;

/* NEW (src/http/ngx_http_upstream.c) */
(void) ngx_http_status_set(r, u->headers_in.status_n);
r->headers_out.status_line = u->headers_in.status_line;
```

Three things make this safe and byte-for-byte unchanged:

- `ngx_http_status_set()` **internally bypasses validation when `r->upstream` is
  set**. Any status a backend returns — including codes outside the registry —
  passes through unchanged, preserving proxy, FastCGI, and other upstream behavior.
- The `(void)` form is **mandatory** here. A proxied code must never trigger the
  `return NGX_HTTP_INTERNAL_SERVER_ERROR;` fallback.
- The adjacent `r->headers_out.status_line = u->headers_in.status_line;` copy is
  **preserved exactly**. When `status_line.len` is non-zero, the header filter emits
  that upstream text verbatim instead of consulting the registry.

## 5. Reason-phrase lookup migration

Previously, the header filter (`src/http/ngx_http_header_filter_module.c`) resolved
reason phrases through a private `static ngx_str_t ngx_http_status_lines[]` table
combined with **per-file offset arithmetic** driven by the macros
`NGX_HTTP_LAST_2XX`, `NGX_HTTP_OFF_3XX/4XX/5XX` (and `NGX_HTTP_LAST_3XX/4XX/5XX`).
That scheme was duplicated in `src/http/ngx_http_special_response.c` with **divergent
values** — `NGX_HTTP_LAST_2XX` was `207` in the header filter but `202` in special
response — the latent inconsistency identified as root cause **RC-3**.

The refactor replaces all of this with a single registry:

- The reason lookup now goes through `ngx_http_status_reason(status)`, which returns
  an `ngx_str_t` holding the **full status-line text** (e.g. `"200 OK"`,
  `"404 Not Found"`), or an **empty** `ngx_str_t` (`.len == 0`) for
  numeric-only/placeholder codes (e.g. 203, 205) and for all 1xx codes. The returned
  `.data` points into `static const` storage, so the value is safe to copy.
- When the reason is empty, the header filter keeps its **numeric-only fallback**
  (`%03ui` — a three-digit, zero-padded code followed by a trailing space), so those
  status lines remain byte-identical.
- The divergent `NGX_HTTP_LAST_2XX` and `NGX_HTTP_OFF_*` macro pairs are **removed
  from both files**, eliminating the 207-versus-202 inconsistency. The registry in
  `src/http/ngx_http_status.c` is now the single source of truth.
- **Wire output — status lines and error-page bodies — remains byte-identical**
  before and after the change.

Conceptually, the header filter now selects between the registry reason and the
numeric-only fallback like this:

```c
ngx_str_t  reason;

reason = ngx_http_status_reason(r->headers_out.status);

if (reason.len) {
    /* emit "HTTP/1.1 " + reason + CRLF, e.g. "HTTP/1.1 404 Not Found\r\n" */
} else {
    /* numeric-only fallback: "HTTP/1.1 " + "%03ui " + CRLF */
}
```

This logic is **internal to the header filter**. Ordinary module authors never call
`ngx_http_status_reason()` directly — they only ever call `ngx_http_status_set()`.

## 6. Checklist for module authors

- [ ] Confirm the enclosing function returns `ngx_int_t` before using the hard-return form; otherwise use the permissive `(void)` form.
- [ ] Pass the numeric code, an existing `NGX_HTTP_*` constant, or an existing `ngx_uint_t` status variable to `ngx_http_status_set()`.
- [ ] Preserve every adjacent field assignment unchanged (for example `r->headers_out.status_line.len = 0;`, `r->headers_out.content_length_n = ...;`, `r->headers_out.last_modified_time = ...;`).
- [ ] Use the permissive `(void) ngx_http_status_set(...)` form on finalize, terminate, and free paths, and on upstream/proxied paths.
- [ ] Never strict-validate a proxied/upstream status — the setter already bypasses validation when `r->upstream` is set, but you must still use the `(void)` form and keep the adjacent `status_line` copy.
- [ ] Do not add a direct `#include "ngx_http_status.h"`; the API is exposed transitively through `ngx_http.h`.
- [ ] Verify byte-parity after migrating: the emitted status line and any error-page body must be unchanged when diffed against a baseline binary.

## Related documentation

- [HTTP Status Code API reference](../api/status_codes.md) — the registry type, flag
  bits, and per-function reference for the same API.
