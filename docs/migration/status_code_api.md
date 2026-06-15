# HTTP Status Code API — Migration Guide

This guide shows NGINX module authors how to migrate direct `r->headers_out.status`
writes to the centralized `ngx_http_status_set()` API introduced by the HTTP
status-code registry refactor. It covers the two build modes (the default
*permissive* build and the opt-in `--with-http_status_validation` *strict* build),
the upstream/proxied pass-through exception, and the reason-phrase lookup change in
the header filter. Wire output — status lines and error-page bodies — is
**byte-identical** before and after migration, and the default build adds **zero
overhead**: `ngx_http_status_set()` is a `static ngx_inline` that compiles down to
the original field write.

## API at a glance

The API is declared in `src/http/ngx_http_status.h` and implemented in
`src/http/ngx_http_status.c`. It is exposed transitively through `ngx_http.h`
(which now `#include`s `ngx_http_status.h`), so **module authors do not add a
direct `#include` of `ngx_http_status.h`** — any translation unit that already
includes `ngx_http.h` can call the API.

| Function | Purpose |
|---|---|
| `ngx_int_t ngx_http_status_set(ngx_http_request_t *r, ngx_uint_t status)` | The assignment chokepoint. Sets `r->headers_out.status`. This is the only call most modules need. |
| `ngx_int_t ngx_http_status_validate(ngx_uint_t status)` | Returns `NGX_OK` when `status` is within the RFC 9110 three-digit range `100..599`, else `NGX_ERROR`. |
| `ngx_str_t ngx_http_status_reason(ngx_uint_t status)` | Returns the full status-line text (e.g. `"404 Not Found"`), or an empty `ngx_str_t` (`.len == 0`) for numeric-only, 1xx, or unknown codes. |
| `ngx_int_t ngx_http_status_register(const ngx_http_status_def_t *def)` | Pre-fork extensibility seam for third-party codes. The registry is read-only after worker initialization. |
| `ngx_uint_t ngx_http_status_is_cacheable(ngx_uint_t status)` | Returns `1` if the code carries the `NGX_HTTP_STATUS_CACHEABLE` metadata flag, else `0`. Metadata only — it introduces no caching behavior. |

The registry descriptor type and its metadata flags are:

```c
typedef struct {
    ngx_uint_t   code;        /* e.g. 404 */
    ngx_str_t    reason;      /* full status-line text, or empty for numeric-only */
    ngx_uint_t   flags;       /* class + cacheability bitmask */
    const char  *rfc_section; /* "15.5.5" */
} ngx_http_status_def_t;

#define NGX_HTTP_STATUS_CACHEABLE       0x0001
#define NGX_HTTP_STATUS_CLIENT_ERROR    0x0002
#define NGX_HTTP_STATUS_SERVER_ERROR    0x0004
#define NGX_HTTP_STATUS_INFORMATIONAL   0x0008
```

Ordinary module code only ever calls `ngx_http_status_set()`. The remaining
functions exist for the header filter (`ngx_http_status_reason()`), for build-time
conformance checks (`ngx_http_status_validate()`), for cache-policy metadata
(`ngx_http_status_is_cacheable()`), and for third-party extension
(`ngx_http_status_register()`).

## 1. Why migrate

Before this refactor (root cause **RC-2**), the response status was written by a
bare struct-field assignment — `r->headers_out.status = NGX_HTTP_*;` — scattered
across roughly 20 modules and 33 distinct call sites. Because each site wrote the
field directly, there was **no single place** to validate a code, attach metadata,
or instrument an assignment: no validation seam, no metadata seam, no
instrumentation seam.

`ngx_http_status_set()` is the **single chokepoint** through which every status
assignment now flows. Routing assignments through one function makes it possible to
add optional RFC 9110 validation, reason-phrase and metadata lookups, and future
instrumentation **without touching the call sites again**.

This is a *structural* refactor with **full backward compatibility**. The default
build behaves exactly as before — same machine code, same wire output. Migrating a
module is therefore free in the common case, and is required only so that every
status assignment passes through the chokepoint.

## 2. Canonical conversion

The canonical migration replaces a direct field write with a guarded call to the
setter. Reproduce this pattern exactly (NGINX style, four-space indentation, no
tabs):

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

Notes on this "hard return" form:

- The enclosing function **must return `ngx_int_t`** for the `return
  NGX_HTTP_INTERNAL_SERVER_ERROR;` branch to be valid. If it does not, use the
  permissive `(void)` form described in [Section 3](#3-permissive-vs-strict-mode).
- In the **default (non-validation) build**, `ngx_http_status_set()` inlines to the
  original field write plus `return NGX_OK`, so the entire error branch
  **constant-folds away** at compile time — zero behavior change and zero added
  overhead.
- You may pass the numeric code (`404`), an existing `NGX_HTTP_*` constant (e.g.
  `NGX_HTTP_OK`, which constant-folds to `200`), or an existing `ngx_uint_t`
  status variable.

### Canonical migrated site — `ngx_http_static_module.c`

The static content handler returns `ngx_int_t`, so it uses the hard-return form.
Its real migration looks like this:

```c
/* OLD (ngx_http_static_module.c) */
r->headers_out.status = NGX_HTTP_OK;
r->headers_out.content_length_n = of.size;
r->headers_out.last_modified_time = of.mtime;

/* NEW (ngx_http_static_module.c) */
if (ngx_http_status_set(r, 200) != NGX_OK) {
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                  "invalid HTTP status %ui", (ngx_uint_t) 200);
    return NGX_HTTP_INTERNAL_SERVER_ERROR;
}
r->headers_out.content_length_n = of.size;
r->headers_out.last_modified_time = of.mtime;
```

`NGX_HTTP_OK` is simply the macro for `200`; either form may be passed to the setter
(a constant argument constant-folds in the default build). The important point:
**only the status write changes.** The adjacent `content_length_n` and
`last_modified_time` assignments stay exactly as they were.

### Existing-variable site — `ngx_http_core_module.c`

When the status is carried by an existing variable, pass the variable through and
preserve any adjacent assignments. The `err_status` path also clears
`status_line.len`, which must be kept:

```c
/* OLD (ngx_http_core_module.c, err_status path) */
if (r->err_status) {
    r->headers_out.status = r->err_status;
    r->headers_out.status_line.len = 0;
}

/* NEW */
if (r->err_status) {
    if (ngx_http_status_set(r, r->err_status) != NGX_OK) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "invalid HTTP status %ui", (ngx_uint_t) r->err_status);
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
    r->headers_out.status_line.len = 0;
}
```

The nginx extension codes (444 and 494–499) are registered, so an `err_status`
carrying an extension code still validates successfully — there is no spurious
`500`. The adjacent `r->headers_out.status_line.len = 0;` is preserved unchanged.

> **Note** — In the actual core module, the standard `NGX_LOG_ERR "invalid HTTP
> status %ui"` record is factored into a small file-local helper so that, under the
> validation build, it can also carry the `$request_id` correlation id. The shape
> is identical (set → on failure: log once and return `500`). Module authors should
> write the inline `ngx_log_error(...)` form shown above.

## 3. Permissive vs strict mode

`ngx_http_status_set()` has two shapes selected at build time.

- **Default build (no flag) — permissive, zero overhead.** `ngx_http_status_set()`
  is a `static ngx_inline` equal to the legacy field write: it performs
  `r->headers_out.status = status;` and returns `NGX_OK`. It never fails, so the
  error branch of the canonical pattern constant-folds away entirely. (Debug builds
  additionally emit a `ngx_log_debug1` status-set trace correlated by the connection
  id; that trace compiles to nothing unless the tree is built `--with-debug`.)
- **`--with-http_status_validation` build — strict.** This defines
  `NGX_HTTP_STATUS_VALIDATION 1` and turns the setter into a real function. A code
  outside the RFC 9110 range `100..599` causes `ngx_http_status_set()` to return
  `NGX_ERROR`. The setter itself does **not** emit `NGX_LOG_ERR`; the **caller**
  logs the single `"invalid HTTP status %ui"` record and falls back to
  `NGX_HTTP_INTERNAL_SERVER_ERROR` — exactly the canonical pattern from
  [Section 2](#2-canonical-conversion).

### Permissive `(void)` form for finalize / terminate paths

On a finalize, terminate, or free path, returning an HTTP error is not valid. Use
the `(void)` form to set the status and intentionally discard the result:

```c
(void) ngx_http_status_set(r, rc);
```

The request finalize and terminate paths in `src/http/ngx_http_request.c` use
exactly this form, for example:

```c
if (rc > 0 && (r->headers_out.status == 0 || r->connection->sent == 0)) {
    (void) ngx_http_status_set(r, rc);
}
```

In the default build this is identical to the old field write. In the validation
build the setter assigns a valid code and the path continues regardless; the
`(void)` cast deliberately ignores the return value because these teardown paths
must never abort on a status check. (For proxied requests the setter passes the
code through unchanged — see [Section 4](#4-upstream-proxied-pass-through-exception).)

## 4. Upstream / proxied pass-through exception

> **Important** — Proxied/upstream status is **copied, never strict-validated.** A
> backend can legitimately return any status code, including codes outside the
> registry; those must reach the client unchanged. Always use the permissive
> `(void)` form on the upstream path and never return an error for a proxied code.

The conversion in `src/http/ngx_http_upstream.c` is:

```c
/* OLD (ngx_http_upstream.c) */
r->headers_out.status = u->headers_in.status_n;
r->headers_out.status_line = u->headers_in.status_line;

/* NEW (ngx_http_upstream.c) */
(void) ngx_http_status_set(r, u->headers_in.status_n);
r->headers_out.status_line = u->headers_in.status_line;
```

Why this is safe:

- `ngx_http_status_set()` **internally bypasses validation when `r->upstream` is
  set**, so any status a backend returns passes through unchanged — even codes that
  are not in the registry. This preserves proxy, FastCGI, uwsgi, SCGI, and gRPC
  upstream behavior byte-for-byte.
- The `(void)` permissive form is mandatory here: never `return` an HTTP error for a
  proxied code.
- The adjacent `r->headers_out.status_line = u->headers_in.status_line;` copy is
  preserved exactly. The header filter emits the upstream's textual status line
  verbatim via its `status_line.len` branch, so the bytes on the wire are identical
  to the unmigrated behavior.

## 5. Reason-phrase lookup migration

This section is relevant to authors who touch the header filter; ordinary modules
do not call the reason API directly.

Previously, the header filter (`src/http/ngx_http_header_filter_module.c`) resolved
reason phrases through a private `static ngx_str_t ngx_http_status_lines[]` table
combined with **per-file offset arithmetic** using the macros `NGX_HTTP_LAST_2XX`,
`NGX_HTTP_OFF_3XX` / `NGX_HTTP_OFF_4XX` / `NGX_HTTP_OFF_5XX` (and
`NGX_HTTP_LAST_3XX` / `NGX_HTTP_LAST_4XX` / `NGX_HTTP_LAST_5XX`). The same scheme was
duplicated in `src/http/ngx_http_special_response.c` with **divergent values** —
`NGX_HTTP_LAST_2XX` was `207` in the header filter but `202` in special response
(root cause **RC-3**), an inconsistency no compiler could catch.

The new approach:

- The reason lookup now goes through `ngx_http_status_reason(status)`, which returns
  an `ngx_str_t` holding the **full status-line text** (e.g. `"200 OK"`,
  `"404 Not Found"`), or an **empty** `ngx_str_t` (`.len == 0`) for numeric-only and
  placeholder codes (e.g. 203, 205) and for all 1xx codes.
- When the reason is empty, the header filter keeps its **numeric-only fallback**
  (`%03ui ` — a three-digit zero-padded code followed by a single space), so those
  status lines remain byte-identical.
- The divergent `NGX_HTTP_LAST_2XX` and `NGX_HTTP_OFF_*` macro pairs are **removed
  from both files**, eliminating the 207-vs-202 inconsistency. The single registry
  in `src/http/ngx_http_status.c` is now the one source of truth.
- **Wire output (status lines and error-page bodies) remains byte-identical** before
  and after.

The header filter's reason handling now looks, in essence, like this:

```c
ngx_str_t  reason;

reason = ngx_http_status_reason(r->headers_out.status);

if (reason.len) {
    /* emit "HTTP/1.1 " + reason + CRLF, e.g. "HTTP/1.1 404 Not Found\r\n" */
} else {
    /* numeric-only fallback: "HTTP/1.1 " + "%03ui " + CRLF */
}
```

This logic is internal to the header filter. Ordinary module authors never call
`ngx_http_status_reason()` themselves — they only call `ngx_http_status_set()`.

## 6. Checklist for module authors

- [ ] Ensure the enclosing function returns `ngx_int_t` before using the hard-return form; otherwise use the permissive `(void)` form.
- [ ] Pass the numeric code, an existing `NGX_HTTP_*` constant, or an existing `ngx_uint_t` status variable to the setter.
- [ ] Preserve any adjacent field assignments unchanged (e.g. `r->headers_out.status_line.len = 0;`, `r->headers_out.content_length_n = ...;`, `r->headers_out.last_modified_time = ...;`).
- [ ] Use the permissive `(void) ngx_http_status_set(...)` form on finalize/terminate/free paths and on upstream/proxied paths.
- [ ] Never strict-validate proxied/upstream status — the setter bypasses validation when `r->upstream` is set.
- [ ] Do not add a direct `#include` of `ngx_http_status.h`; it is exposed transitively via `ngx_http.h`.
- [ ] Verify byte-parity after migrating: the emitted status line and any error-page body must be unchanged (diff against a baseline binary).

## Related documentation

- [HTTP Status Code API reference](../api/status_codes.md)

