# HTTP Status Code API Reference

**Project:** NGINX 1.29.5 Centralized HTTP Status Code Refactor
**Audience:** NGINX core developers, third-party module authors, distribution maintainers
**Companion documents:** [migration guide](../migration/status_code_api.md), [decision log](../architecture/decision_log.md), [architecture diagrams](../architecture/status_code_refactor.md), [observability](../architecture/observability.md)

## Overview

NGINX 1.29.5 introduces a **centralized HTTP status code API** consisting of five public C functions and a static registry containing all RFC 9110 § 15 standard status codes plus NGINX-specific 4xx extensions. The API is additive — every existing `#define NGX_HTTP_*` macro and every direct assignment to `r->headers_out.status` continues to function identically. New code is encouraged to use the API for the four benefits documented in [Build Modes](#build-modes-permissive-vs-strict): range validation, RFC 9110 conformance checking, structured debug tracing, and future extensibility.

The five public functions:

| Function | One-line summary |
|---|---|
| [`ngx_http_status_set`](#ngx_http_status_set) | Assign the response status with optional validation. |
| [`ngx_http_status_validate`](#ngx_http_status_validate) | Pure range check (100–599 inclusive) per RFC 9110. |
| [`ngx_http_status_reason`](#ngx_http_status_reason) | Look up the canonical RFC 9110 reason phrase for a status code. |
| [`ngx_http_status_is_cacheable`](#ngx_http_status_is_cacheable) | Query the cacheability flag (RFC 9111 § 3) for a status code. |
| [`ngx_http_status_register`](#ngx_http_status_register) | Register a custom status code (init-phase only). |

The registry contains approximately **58 entries** spanning RFC 9110 standard codes (1xx through 5xx) and 6 NGINX-specific 4xx extensions (444, 494, 495, 496, 497, 499). Each entry carries the numeric code, the canonical RFC 9110 reason phrase, a class/flag bitmask, and a textual RFC section reference for compliance auditing. The exact count varies by ±1 depending on whether the registry includes 203 Non-Authoritative Information as a registry-only entry; see the [Status Code Registry](#status-code-registry) section for the complete enumeration.

This document is the **definitive API contract reference** — the "what" of the API. It enumerates every public symbol, every parameter, every return value, every side effect, every error condition, every behavioral mode, and every registered status code. For migration guidance (the "how" of adopting the API), see the [migration guide](../migration/status_code_api.md). For architectural decisions and rationale, see the [decision log](../architecture/decision_log.md).

## Header Files and Inclusion

The API surface is split between two headers per [decision D-002](../architecture/decision_log.md#architectural-decisions):

| Header | Contents |
|---|---|
| `<ngx_http.h>` | Public function prototypes for all five `ngx_http_status_*` functions. Already included by every HTTP module. Includes `<ngx_http_status.h>` transitively. |
| `<ngx_http_status.h>` | Type definition `ngx_http_status_def_t`, flag-bit constants (`NGX_HTTP_STATUS_CACHEABLE` etc.), and internal helper prototypes. |

**Recommended inclusion pattern (no change from existing modules):**

```c
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
```

This pattern brings in the API prototypes and type definitions transitively. No additional `#include` directive is required in consumer modules.

**Feature-detection macro for cross-version source compatibility:**

The flag-bit constant `NGX_HTTP_STATUS_CACHEABLE` is defined in `<ngx_http_status.h>` and is therefore reachable transitively in NGINX 1.29.5+. It is **not** defined in earlier NGINX versions. Modules that need to compile against both pre-refactor and post-refactor NGINX can use:

```c
#if defined(NGX_HTTP_STATUS_CACHEABLE)
#  define HAS_NGX_HTTP_STATUS_API 1
#else
#  define HAS_NGX_HTTP_STATUS_API 0
#endif
```

See [the migration guide](../migration/status_code_api.md) for a worked example.

## Type Definitions

### `ngx_http_status_def_t`

Each registry entry is a value of type `ngx_http_status_def_t`. The struct definition is:

```c
typedef struct {
    ngx_uint_t    code;          // HTTP status code (100-599)
    ngx_str_t     reason;        // Reason phrase per RFC 9110
    ngx_uint_t    flags;         // NGX_HTTP_STATUS_CACHEABLE | CLIENT_ERROR | SERVER_ERROR
    const char   *rfc_section;   // RFC 9110 section reference
} ngx_http_status_def_t;
```

| Field | Type | Description |
|---|---|---|
| `code` | `ngx_uint_t` | The numeric HTTP status code in the range 100–599. Acts as the primary key for registry lookup. |
| `reason` | `ngx_str_t` | The canonical RFC 9110 reason phrase as an `ngx_str_t` (length-prefixed C string). Constructed via `ngx_string("...")` macro at compile time; not heap-allocated. |
| `flags` | `ngx_uint_t` | A bitmask combining the [flag bits](#flag-bits) below to encode classification (informational, client error, server error), cacheability, and NGINX-extension marker. |
| `rfc_section` | `const char *` | A null-terminated ASCII string identifying the originating standard (e.g., `"RFC 9110 §15.3.1"` for code 200, or `"nginx extension"` for codes 444/494/495/496/497/499). |

**Stability contract:** The fields and their order are an implementation detail. Modules MUST NOT rely on `sizeof(ngx_http_status_def_t)`, field offsets, or the relative ordering of entries within the registry. Use [`ngx_http_status_register`](#ngx_http_status_register) (during the configuration phase only) to add custom entries; use [`ngx_http_status_reason`](#ngx_http_status_reason) and [`ngx_http_status_is_cacheable`](#ngx_http_status_is_cacheable) to query existing entries.

### Flag Bits

The `flags` field of `ngx_http_status_def_t` is a bitmask composed of zero or more of the following flag bits, defined in `<ngx_http_status.h>`:

| Flag | Value | Meaning |
|---|---|---|
| `NGX_HTTP_STATUS_CACHEABLE` | `0x01` | The response is cacheable by default per RFC 9111 § 3 (e.g., 200, 203, 204, 206, 300, 301, 308, 404, 405, 410, 414, 451, 501). Used by [`ngx_http_status_is_cacheable`](#ngx_http_status_is_cacheable). |
| `NGX_HTTP_STATUS_CLIENT_ERROR` | `0x02` | The response is a 4xx client error per RFC 9110 § 15.5. Set on every 4xx code including NGINX-specific extensions. |
| `NGX_HTTP_STATUS_SERVER_ERROR` | `0x04` | The response is a 5xx server error per RFC 9110 § 15.6. Set on every 5xx code. |
| `NGX_HTTP_STATUS_INFORMATIONAL` | `0x08` | The response is a 1xx informational provisional response per RFC 9110 § 15.2. Provisional responses do not signal completion of a request and may be followed by zero or more additional 1xx responses and exactly one final 2xx–5xx response. |
| `NGX_HTTP_STATUS_NGINX_EXT` | `0x10` | The code is an NGINX-specific extension, not registered with IANA. Currently set on 444, 494, 495, 496, 497, 499. |

**Mutual exclusivity:** A given code carries at most one of `NGX_HTTP_STATUS_INFORMATIONAL`, `NGX_HTTP_STATUS_CLIENT_ERROR`, or `NGX_HTTP_STATUS_SERVER_ERROR`. 2xx and 3xx codes carry none of these three (only `NGX_HTTP_STATUS_CACHEABLE` where applicable). The five flags can be tested independently using `&`:

```c
if (def->flags & NGX_HTTP_STATUS_CACHEABLE) { /* response is cacheable */ }
if (def->flags & NGX_HTTP_STATUS_NGINX_EXT) { /* NGINX-specific code */ }
```

## Public Function Reference

Each of the five public functions is documented below with its full signature, parameters, return values, side effects, error conditions, threading semantics, build-mode behavior (where applicable), and a representative code example. The four primary signatures (`ngx_http_status_set`, `ngx_http_status_validate`, `ngx_http_status_reason`, `ngx_http_status_register`) are reproduced verbatim from AAP § 0.8.8 including their docstring comments. The fifth function (`ngx_http_status_is_cacheable`) is documented with its canonical signature.

### `ngx_http_status_set`

```c
// Set response status code with validation
// Returns: NGX_OK on success, NGX_ERROR if status invalid
// Side effects: Sets r->headers_out.status, logs validation failures
ngx_int_t ngx_http_status_set(ngx_http_request_t *r, ngx_uint_t status);
```

Assigns the response status code on the supplied request, performing range validation and (in strict builds) RFC 9110 conformance checks. This is the primary entry point for centralized status assignment and is the recommended replacement for direct `r->headers_out.status = status;` writes.

**Parameters:**

| Parameter | Type | Description |
|---|---|---|
| `r` | `ngx_http_request_t *` | The request context. MUST NOT be NULL. The function reads `r->upstream` and `r->connection->log`, and writes `r->headers_out.status`. |
| `status` | `ngx_uint_t` | The proposed status code. Ideally in the range 100–599 inclusive per RFC 9110. Out-of-range codes have build-mode-dependent behavior described below. |

**Returns:**

| Return | Meaning |
|---|---|
| `NGX_OK` (0) | Status was accepted and assigned. In permissive mode, this is returned for all in-range codes and (for compatibility) for out-of-range codes. In strict mode, returned only when all checks pass. |
| `NGX_ERROR` (-1) | Status was rejected and NOT assigned. Strict mode only — never returned in permissive builds for in-range codes. |

**Side effects:**

- **Always:** writes `status` to `r->headers_out.status` if validation passes.
- **Always (with `--with-debug`):** emits one `NGX_LOG_DEBUG_HTTP` line in the form `"http status set: %ui %V (strict=%s upstream=%s)"` encoding the code, the registered reason phrase, the build-mode flag, and whether the request has an upstream.
- **Strict mode + invalid code:** emits `NGX_LOG_WARN` with an RFC § reference identifying the violated rule.
- **Strict mode + 1xx-after-final:** emits `NGX_LOG_ERR`.

**Errors (strict mode only):**

| Condition | Log level | Outcome |
|---|---|---|
| Out-of-range code (`< 100` or `> 599`) | `NGX_LOG_WARN` with RFC § reference | Returns `NGX_ERROR`; `r->headers_out.status` is NOT modified. |
| 1xx code emitted after a final (2xx–5xx) response on same request | `NGX_LOG_ERR` | Returns `NGX_ERROR`; `r->headers_out.status` is NOT modified. |

**Threading:** Safe to call from any worker process. The function reads the immutable static registry; no shared-state mutation occurs.

**Permissive vs Strict:**

- **Permissive (default build):** Always returns `NGX_OK` for in-range codes; out-of-range codes log at `NGX_LOG_DEBUG_HTTP` and are still assigned (compatibility with third-party modules emitting experimental codes); for compile-time-constant arguments, the compiler inlines the call to a direct field write with no observable overhead.
- **Strict (`--with-http_status_validation`):** Range check, 1xx-after-final check, and single-final-code enforcement run; rejections return `NGX_ERROR`. See [Build Modes](#build-modes-permissive-vs-strict).

**Upstream bypass:** When `r->upstream != NULL`, all strict-mode validation is bypassed. The status is assigned unconditionally, preserving upstream pass-through semantics per [decision D-006](../architecture/decision_log.md#architectural-decisions). This ensures that misbehaving backends emitting non-RFC codes (e.g., 999) do not cause NGINX to drop responses.

**Caller pattern:** For runtime-variable status, check the return and fall back to `NGX_HTTP_INTERNAL_SERVER_ERROR` (500) on rejection. The canonical migration pattern from AAP § 0.1.2 is:

```c
// OLD PATTERN (deprecated):
r->headers_out.status = NGX_HTTP_NOT_FOUND;

// NEW PATTERN (required):
if (ngx_http_status_set(r, 404) != NGX_OK) {
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                  "invalid status code: 404");
    return NGX_HTTP_INTERNAL_SERVER_ERROR;
}
```

For compile-time constants known to be in-range (`NGX_HTTP_OK`, `NGX_HTTP_NOT_FOUND`, etc.), the call may be cast `(void)` because failure is impossible:

```c
// Compile-time constants in-range — return value is intentionally discarded
(void) ngx_http_status_set(r, NGX_HTTP_OK);
```


### `ngx_http_status_validate`

```c
// Validate status code against RFC 9110 rules
// Returns: NGX_OK if valid (100-599), NGX_ERROR otherwise
ngx_int_t ngx_http_status_validate(ngx_uint_t status);
```

A pure range check returning whether `status` falls within the RFC 9110 status-code range. This function does not require an `ngx_http_request_t` context and performs no logging or side effects, making it suitable for pre-flight validation in any context, including upstream-response parsing harnesses and fuzzing front-ends.

**Parameters:**

| Parameter | Type | Description |
|---|---|---|
| `status` | `ngx_uint_t` | The status code to validate. |

**Returns:**

| Return | Condition |
|---|---|
| `NGX_OK` (0) | `status >= 100 && status <= 599`. |
| `NGX_ERROR` (-1) | Otherwise (out-of-range). |

**Side effects:** None. Pure function.

**Errors:** None — the return value encodes the validation outcome.

**Threading:** Re-entrant. Safe in any context, including signal handlers (though that is not a typical NGINX usage).

**Permissive vs Strict:** Behavior is identical in both modes — this is a pure range check. Strict mode does NOT extend this function with additional checks. The additional contextual checks (1xx-after-final, single-final-code) live inside [`ngx_http_status_set`](#ngx_http_status_set) because they require an `ngx_http_request_t *` for context.

**Use cases:**

- Pre-flight check before complex logic that depends on a runtime status value.
- Static analysis or fuzzing harnesses validating arbitrary inputs.
- Defensive validation of values parsed from untrusted sources before use as a registry lookup key.

**Code example:**

```c
ngx_uint_t  status = parse_status_from_header(buf);

if (ngx_http_status_validate(status) != NGX_OK) {
    ngx_log_error(NGX_LOG_WARN, r->connection->log, 0,
                  "received malformed status from upstream: %ui", status);
    return NGX_ERROR;
}
```

### `ngx_http_status_reason`

```c
// Retrieve reason phrase for status code
// Returns: Pointer to reason string, or default "Unknown" if not registered
const ngx_str_t *ngx_http_status_reason(ngx_uint_t status);
```

Looks up the canonical RFC 9110 reason phrase associated with a status code. Implements the Null Object pattern per [decision D-007](../architecture/decision_log.md#architectural-decisions): the function never returns NULL. Unregistered codes resolve to a sentinel string `"Unknown"`, eliminating defensive NULL checks in callers.

**Parameters:**

| Parameter | Type | Description |
|---|---|---|
| `status` | `ngx_uint_t` | The status code to look up. |

**Returns:**

| Return | Condition |
|---|---|
| Pointer to the registry entry's `reason` field | The code is registered (in the static registry or via [`ngx_http_status_register`](#ngx_http_status_register) during init). |
| Pointer to the sentinel `&ngx_http_status_unknown_reason` (with `ngx_string("Unknown")`) | The code is not registered. |

**Never returns NULL.**

**Side effects:** None.

**Errors:** None — unregistered codes return the sentinel rather than failing.

**Threading:** Re-entrant. Reads the immutable static registry only.

**Wire vs Registry phrase distinction:** The phrase returned by this function is the **canonical RFC 9110 phrase** (e.g., `"Found"` for 302). It MAY differ from the on-wire HTTP/1.1 reason phrase emitted by the `ngx_http_status_lines[]` wire table (which retains legacy NGINX wording for backward compatibility). For the six material divergences and two minor variants, see [Wire Phrase vs Registry Phrase Divergences](#wire-phrase-vs-registry-phrase-divergences).

**Use cases:**

- Constructing structured log lines that include the canonical reason phrase.
- Programmatic introspection of status semantics.
- Building RFC-compliant alternative response surfaces (e.g., a JSON error envelope).

**Code example:**

```c
const ngx_str_t  *reason = ngx_http_status_reason(404);

ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
               "looked up reason phrase: code=%ui reason=\"%V\"",
               (ngx_uint_t) 404, reason);
/* Output: "looked up reason phrase: code=404 reason="Not Found"" */
```

### `ngx_http_status_is_cacheable`

```c
// Query whether a status code is cacheable by default per RFC 9111 § 3
// Returns: 1 if cacheable, 0 otherwise (including unregistered codes)
ngx_uint_t ngx_http_status_is_cacheable(ngx_uint_t status);
```

Returns whether a status code is cacheable by default per RFC 9111 § 3. Equivalent to testing the `NGX_HTTP_STATUS_CACHEABLE` flag bit on the registry entry for `status`.

**Parameters:**

| Parameter | Type | Description |
|---|---|---|
| `status` | `ngx_uint_t` | The status code to query. |

**Returns:**

| Return | Condition |
|---|---|
| `1` | `status` is registered AND its entry carries the `NGX_HTTP_STATUS_CACHEABLE` flag. |
| `0` | Otherwise (including for codes not in the registry). |

**Side effects:** None.

**Errors:** None — unregistered codes return 0.

**Threading:** Re-entrant. Reads the immutable static registry only.

**Cacheable codes (per RFC 9111 § 3 and the registry):** 200, 203, 204, 206, 300, 301, 308, 404, 405, 410, 414, 451, 501. All other codes return 0.

**Use cases:**

- Cache-key construction logic deciding whether to compute a cache lookup.
- Cache-eligibility decisions before invoking the file-cache subsystem.
- Programmatic introspection of cacheability semantics.

**Code example:**

```c
if (ngx_http_status_is_cacheable(r->headers_out.status)) {
    /* eligible for cache storage */
    ngx_http_file_cache_create(r);
}
```

### `ngx_http_status_register`

```c
// Register custom status code (for extensibility)
// Returns: NGX_OK on success, NGX_ERROR if code already registered
ngx_int_t ngx_http_status_register(ngx_http_status_def_t *def);
```

Registers a custom status code at configuration parsing time. This function is the controlled extension point for distribution-specific or vendor-specific status codes. Per [decision D-004](../architecture/decision_log.md#architectural-decisions) and the AAP behavioral prohibition "Never create registry modification APIs accessible post-initialization," the function is callable only during the configuration phase; post-init calls return `NGX_ERROR`.

**Parameters:**

| Parameter | Type | Description |
|---|---|---|
| `def` | `ngx_http_status_def_t *` | Pointer to a fully-initialized registry entry. The pointed-to structure must remain valid for the lifetime of the NGINX process; in practice, define it as a file-scope or stack-during-init value whose contents are copied into the registry. |

**Returns:**

| Return | Condition |
|---|---|
| `NGX_OK` (0) | The code was successfully registered. |
| `NGX_ERROR` (-1) | One of the failure conditions listed below was triggered. |

**Side effects:** When `NGX_OK` is returned, an entry is added to the registry's auxiliary extension array. No heap allocation occurs in worker processes; the registry's primary static array is unchanged.

**Errors:**

| Condition | Outcome |
|---|---|
| `def == NULL` | Returns `NGX_ERROR`. |
| `def->code` outside 100–599 | Returns `NGX_ERROR`. |
| `def->code` already in the registry (either statically or via a prior call to this function) | Returns `NGX_ERROR`. |
| Called after worker fork (post-init) | Returns `NGX_ERROR`. The function checks a module-level "init done" flag set after `ngx_http_status_init_registry()` completes. |

**Threading:** NOT thread-safe. Must be called only during single-threaded configuration parsing (init phase) in the master process. After worker fork, the registry is read-only by design. Per [decision D-004](../architecture/decision_log.md#architectural-decisions), the registry's primary array lives in `.rodata`.

**Init-phase only constraint:** Per AAP § 0.8.4, the registry initialization must complete during the configuration parsing phase only. A module-level flag is checked inside the function; post-init calls return `NGX_ERROR`. This satisfies the AAP behavioral prohibition "Never create registry modification APIs accessible post-initialization."

**Use cases:**

- Distribution-specific custom codes (e.g., a vendor-distribution that ships with extra status codes for their internal monitoring).
- Vendor extensions exposed through `nginx.conf` directives provided by third-party modules.
- Experimental codes during plugin development.

**Most modules will NOT need this function.** The static registry already covers all RFC 9110 § 15 standard codes plus the six NGINX-specific 4xx extensions.

**Code example (correct usage during a `preconfiguration` callback):**

```c
static ngx_int_t
my_module_preconfiguration(ngx_conf_t *cf)
{
    ngx_http_status_def_t  custom_code = {
        .code = 599,                                  /* Application-specific 5xx */
        .reason = ngx_string("My Custom Server Error"),
        .flags = NGX_HTTP_STATUS_SERVER_ERROR | NGX_HTTP_STATUS_NGINX_EXT,
        .rfc_section = "vendor extension"
    };

    if (ngx_http_status_register(&custom_code) != NGX_OK) {
        ngx_log_error(NGX_LOG_EMERG, cf->log, 0,
                      "failed to register custom status 599");
        return NGX_ERROR;
    }

    return NGX_OK;
}
```

## Build Modes (Permissive vs Strict)

The validation depth of `ngx_http_status_set()` is controlled by the `--with-http_status_validation` configure flag, which defines the preprocessor macro `NGX_HTTP_STATUS_VALIDATION=1` in `objs/ngx_auto_config.h`. Per [decision D-005](../architecture/decision_log.md#architectural-decisions), the flag is **off by default**; default builds retain permissive behavior.

| Behavior | Permissive Build (default) | Strict Build (`--with-http_status_validation`) |
|---|---|---|
| Configure flag | (none — default) | `./auto/configure --with-http_status_validation` |
| Preprocessor symbol | (undefined) | `NGX_HTTP_STATUS_VALIDATION=1` |
| In-range code (100–599) | Assigned; returns `NGX_OK` | Assigned; returns `NGX_OK` |
| Out-of-range code (`<100` or `>599`) | Logged at `NGX_LOG_DEBUG`; **still assigned**; returns `NGX_OK` | Logged at `NGX_LOG_WARN` with RFC § reference; **NOT assigned**; returns `NGX_ERROR` |
| 1xx code after a final 2xx–5xx response | Allowed (no detection); returns `NGX_OK` | Logged at `NGX_LOG_ERR`; **NOT assigned**; returns `NGX_ERROR` |
| Multiple final-code assignments per request | Allowed (last wins, matching pre-refactor behavior) | Allowed in V1; future enhancement |
| Upstream pass-through (`r->upstream != NULL`) | Bypasses validation | Bypasses validation (same as permissive) |
| Compile-time constant in valid range | Compiler folds the call to a direct `mov` | Compiler folds the call to a direct `mov` |
| Debug log line emitted | Yes (only when built with `--with-debug`) | Yes (only when built with `--with-debug`) |
| Compatibility | Backward-compatible with all third-party modules | May reject codes from modules emitting non-RFC codes |

**Why permissive is default:** Off-by-default ensures backward compatibility for existing nginx.conf deployments where third-party modules may emit non-standard codes. Strict mode is opt-in for nginx builders who want RFC 9110 conformance in their distribution.

**Permissive-mode behavior summary (per AAP § 0.8.7):**

- Accept status codes in 100–599 range (permissive).
- Log suspicious codes at `NGX_LOG_DEBUG` level only.
- Preserve full backward compatibility.

**Strict-mode behavior summary (per AAP § 0.8.7):**

- Reject invalid status codes (return `NGX_ERROR`).
- Log RFC 9110 violations at `NGX_LOG_WARN` level.
- Provide a detailed violation message referencing the RFC section.

**Performance impact (per AAP § 0.7.6):**

- **Permissive:** For compile-time-constant arguments, the compiler inlines `ngx_http_status_set()` and folds the range check to a no-op, yielding identical generated code to direct field assignment. Estimated overhead: **0 CPU cycles** per call.
- **Strict:** Range check + class lookup + (conditional) reason-phrase scan adds approximately **30–60 CPU cycles** per call. Acceptable because strict mode is opt-in.

The latency gate from AAP § 0.8.1 R-8 (<2% on `wrk -t4 -c100 -d30s`) is satisfied in permissive builds. See [decision log § Performance Impact](../architecture/decision_log.md#performance-impact) for the measurement methodology and results.


## Behavioral Contract and Invariants

The following invariants are contractual and apply to all NGINX 1.29.5+ releases until explicitly versioned out by a major release announcement.

### Backward Compatibility

| Invariant | Anchor | Implication |
|---|---|---|
| All `#define NGX_HTTP_*` macros remain defined at their current numeric values | AAP § 0.8.1 R-1; `src/http/ngx_http_request.h` lines 74–145 | `r->headers_out.status = NGX_HTTP_NOT_FOUND;` continues to compile and produce identical wire output |
| Direct field assignment (`r->headers_out.status = X;`) is preserved | AAP § 0.8.1 R-6 | The field is not deprecated; existing modules need not migrate |
| `NGX_MODULE_V1` ABI is unchanged | AAP § 0.8.1 R-2 | Pre-compiled `.so` modules built against pristine 1.29.5 headers continue to load |
| Filter chain interfaces unchanged | AAP § 0.8.1 R-4 | `ngx_http_output_header_filter_pt`, `ngx_http_output_body_filter_pt` signatures preserved |
| `nginx.conf` directive behavior unchanged | AAP § 0.8.5 | `error_page`, `return`, `proxy_intercept_errors` parsers preserved byte-for-byte |
| Wire format unchanged | AAP D-008 | `ngx_http_status_lines[]` retains legacy NGINX wording; HTTP/1.1 clients see identical status lines |
| `$status` and `$upstream_status` access-log variables unchanged | AAP § 0.3.2 B | Log analysis tools and dashboards continue to work without changes |

### Functional Invariants

| Invariant | Rationale |
|---|---|
| Upstream pass-through bypasses strict validation | NGINX MUST NOT alter upstream-emitted status codes (D-006). The `r->upstream != NULL` branch unconditionally accepts the assignment in both permissive and strict builds. |
| Reason-phrase lookup is a Null Object | `ngx_http_status_reason()` never returns NULL; unregistered codes return the sentinel `"Unknown"` (D-007). |
| Registry is immutable after worker fork | Registration is restricted to the configuration phase. Post-init calls to `ngx_http_status_register()` return `NGX_ERROR`. |
| Registry storage is `.rodata` | Per [decision D-004](../architecture/decision_log.md#architectural-decisions), the registry is statically initialized at compile time and shared across worker processes via copy-on-write page sharing. No per-worker heap allocation. |
| All `ngx_http_status_*` functions are re-entrant | All five functions read the immutable registry; no global mutable state is touched. Safe to call from any callback. |
| `error_page` directive parser is preserved byte-for-byte | The 300–599 range check, 499 rejection, and 494/495/496/497→400 masquerade in `ngx_http_core_error_page()` are not replaced by the new API. |

## Behavioral Prohibitions (per AAP § 0.8.2 — preserved verbatim)

The API explicitly does NOT support the following capabilities. These are not implementation gaps; they are deliberate, contractual commitments to API simplicity and ABI stability that the registry maintainers will refuse to relax in future revisions without a major-version ABI break.

The API explicitly does NOT support:

- Caching mechanisms for status code lookups beyond direct array indexing
- Status code transformations during upstream proxying (pass-through only)
- Registry modification APIs accessible post-initialization
- Optimization of the registry structure beyond the static array
- Modification of existing `error_page` directive parsing logic
- Thread-local status code storage
- Custom reason phrase generation beyond registry lookup
- Status code aliasing functionality

Each prohibition has an architectural rationale recorded in the [Decision Log](../architecture/decision_log.md): D-004 anchors the static-array storage choice (forbidding more elaborate caching), D-006 anchors the upstream pass-through invariant (forbidding transformation), and D-008 anchors the wire-vs-registry phrase split (forbidding custom phrase generation). Operators auditing for compliance with the stated prohibitions can rely on the registry's source-level immutability after initialization (a single boolean guard in `ngx_http_status_register()`) as the runtime enforcement mechanism.

## Status Code Registry

The registry contains entries covering the standard RFC 9110 § 15 codes plus 6 NGINX-specific 4xx extensions. Each entry carries the numeric code, the canonical RFC 9110 reason phrase, classification flags (per [Flag Bits](#flag-bits)), and a textual RFC section reference for compliance auditing. The complete enumeration follows in the per-class subsections below; see the registry-total summary at the end of this section for the precise count.

**Legend for the tables below:**

- The `Reason Phrase (Registry)` column is the **canonical RFC 9110 phrase** returned by [`ngx_http_status_reason`](#ngx_http_status_reason). For 6 codes, this differs from the wire phrase emitted on HTTP/1.1; see [Wire Phrase vs Registry Phrase Divergences](#wire-phrase-vs-registry-phrase-divergences).
- The `Flags` column lists flag bits set on the entry; codes with no flags are 2xx/3xx success/redirect with no special classification.
- The `Macro` column shows the corresponding `NGX_HTTP_*` macro from `src/http/ngx_http_request.h`, or `(none)` if no macro exists for the code (it is registry-only).

### 1xx Informational

Provisional responses indicating the request was received and processing continues. Per RFC 9110 § 15.2, a 1xx response does not constitute completion of a request and may be followed by zero or more additional 1xx responses and exactly one final 2xx–5xx response. NGINX permits 1xx emission before final response only; in strict mode, a 1xx after a final response is rejected with `NGX_LOG_ERR`.

| Code | Reason Phrase (Registry) | Flags | RFC Section | Macro |
|---|---|---|---|---|
| 100 | Continue | `INFORMATIONAL` | RFC 9110 §15.2.1 | `NGX_HTTP_CONTINUE` |
| 101 | Switching Protocols | `INFORMATIONAL` | RFC 9110 §15.2.2 | `NGX_HTTP_SWITCHING_PROTOCOLS` |
| 102 | Processing | `INFORMATIONAL` | RFC 2518 §10.1 | `NGX_HTTP_PROCESSING` |
| 103 | Early Hints | `INFORMATIONAL` | RFC 8297 §2 | `NGX_HTTP_EARLY_HINTS` |

### 2xx Successful

Final responses indicating the request was successfully received, understood, and accepted. Per RFC 9110 § 15.3.

| Code | Reason Phrase (Registry) | Flags | RFC Section | Macro |
|---|---|---|---|---|
| 200 | OK | `CACHEABLE` | RFC 9110 §15.3.1 | `NGX_HTTP_OK` |
| 201 | Created | (none) | RFC 9110 §15.3.2 | `NGX_HTTP_CREATED` |
| 202 | Accepted | (none) | RFC 9110 §15.3.3 | `NGX_HTTP_ACCEPTED` |
| 203 | Non-Authoritative Information | `CACHEABLE` | RFC 9110 §15.3.4 | (none) |
| 204 | No Content | `CACHEABLE` | RFC 9110 §15.3.5 | `NGX_HTTP_NO_CONTENT` |
| 206 | Partial Content | `CACHEABLE` | RFC 9110 §15.3.7 | `NGX_HTTP_PARTIAL_CONTENT` |

### 3xx Redirection

Final responses indicating that further action is needed to complete the request. Per RFC 9110 § 15.4.

| Code | Reason Phrase (Registry) | Flags | RFC Section | Macro |
|---|---|---|---|---|
| 300 | Multiple Choices | `CACHEABLE` | RFC 9110 §15.4.1 | `NGX_HTTP_SPECIAL_RESPONSE` (used as a threshold sentinel; see Note below) |
| 301 | Moved Permanently | `CACHEABLE` | RFC 9110 §15.4.2 | `NGX_HTTP_MOVED_PERMANENTLY` |
| 302 | Found | (none) | RFC 9110 §15.4.3 | `NGX_HTTP_MOVED_TEMPORARILY` |
| 303 | See Other | (none) | RFC 9110 §15.4.4 | `NGX_HTTP_SEE_OTHER` |
| 304 | Not Modified | (none) | RFC 9110 §15.4.5 | `NGX_HTTP_NOT_MODIFIED` |
| 307 | Temporary Redirect | (none) | RFC 9110 §15.4.8 | `NGX_HTTP_TEMPORARY_REDIRECT` |
| 308 | Permanent Redirect | `CACHEABLE` | RFC 9110 §15.4.9 | `NGX_HTTP_PERMANENT_REDIRECT` |

**Note on `NGX_HTTP_SPECIAL_RESPONSE`:** The macro `NGX_HTTP_SPECIAL_RESPONSE` (value 300) is used in core NGINX as a **threshold sentinel** marking "the first 3xx code." It is rarely emitted as a literal status; instead, code such as `if (status >= NGX_HTTP_SPECIAL_RESPONSE)` distinguishes redirect/error responses from successful ones. The registry entry for 300 ensures that explicit emission via `ngx_http_status_set(r, 300)` produces the canonical RFC 9110 phrase "Multiple Choices."

**Method-preservation note (RFC 9110 § 15.4):** Codes 301 and 302 historically had ambiguous behavior — many clients converted POST-following-redirect to GET. Codes 307 and 308 were introduced to **guarantee method preservation**. For API redirects involving POST/PUT/PATCH/DELETE, prefer 307 (temporary) or 308 (permanent) over 302/301.

**Note on 302 phrase:** The registry uses the RFC 9110 canonical phrase "Found" for 302. The on-wire HTTP/1.1 emitted phrase remains "Moved Temporarily" for backward compatibility per [decision D-008](../architecture/decision_log.md#architectural-decisions). See [Wire Phrase vs Registry Phrase Divergences](#wire-phrase-vs-registry-phrase-divergences).

### 4xx Client Error (Standard)

Final responses indicating that the client appears to have made a request that the server cannot or will not process. Per RFC 9110 § 15.5.

| Code | Reason Phrase (Registry) | Flags | RFC Section | Macro |
|---|---|---|---|---|
| 400 | Bad Request | `CLIENT_ERROR` | RFC 9110 §15.5.1 | `NGX_HTTP_BAD_REQUEST` |
| 401 | Unauthorized | `CLIENT_ERROR` | RFC 9110 §15.5.2 | `NGX_HTTP_UNAUTHORIZED` |
| 402 | Payment Required | `CLIENT_ERROR` | RFC 9110 §15.5.3 | (none) |
| 403 | Forbidden | `CLIENT_ERROR` | RFC 9110 §15.5.4 | `NGX_HTTP_FORBIDDEN` |
| 404 | Not Found | `CLIENT_ERROR \| CACHEABLE` | RFC 9110 §15.5.5 | `NGX_HTTP_NOT_FOUND` |
| 405 | Method Not Allowed | `CLIENT_ERROR \| CACHEABLE` | RFC 9110 §15.5.6 | `NGX_HTTP_NOT_ALLOWED` |
| 406 | Not Acceptable | `CLIENT_ERROR` | RFC 9110 §15.5.7 | (none) |
| 408 | Request Timeout | `CLIENT_ERROR` | RFC 9110 §15.5.9 | `NGX_HTTP_REQUEST_TIME_OUT` |
| 409 | Conflict | `CLIENT_ERROR` | RFC 9110 §15.5.10 | `NGX_HTTP_CONFLICT` |
| 410 | Gone | `CLIENT_ERROR \| CACHEABLE` | RFC 9110 §15.5.11 | (none) |
| 411 | Length Required | `CLIENT_ERROR` | RFC 9110 §15.5.12 | `NGX_HTTP_LENGTH_REQUIRED` |
| 412 | Precondition Failed | `CLIENT_ERROR` | RFC 9110 §15.5.13 | `NGX_HTTP_PRECONDITION_FAILED` |
| 413 | Content Too Large | `CLIENT_ERROR` | RFC 9110 §15.5.14 | `NGX_HTTP_REQUEST_ENTITY_TOO_LARGE` |
| 414 | URI Too Long | `CLIENT_ERROR \| CACHEABLE` | RFC 9110 §15.5.15 | `NGX_HTTP_REQUEST_URI_TOO_LARGE` |
| 415 | Unsupported Media Type | `CLIENT_ERROR` | RFC 9110 §15.5.16 | `NGX_HTTP_UNSUPPORTED_MEDIA_TYPE` |
| 416 | Range Not Satisfiable | `CLIENT_ERROR` | RFC 9110 §15.5.17 | `NGX_HTTP_RANGE_NOT_SATISFIABLE` |
| 417 | Expectation Failed | `CLIENT_ERROR` | RFC 9110 §15.5.18 | (none) |
| 421 | Misdirected Request | `CLIENT_ERROR` | RFC 9110 §15.5.20 | `NGX_HTTP_MISDIRECTED_REQUEST` |
| 422 | Unprocessable Content | `CLIENT_ERROR` | RFC 9110 §15.5.21 | (none) |
| 425 | Too Early | `CLIENT_ERROR` | RFC 8470 §5.2 | (none) |
| 426 | Upgrade Required | `CLIENT_ERROR` | RFC 9110 §15.5.22 | (none) |
| 428 | Precondition Required | `CLIENT_ERROR` | RFC 6585 §3 | (none) |
| 429 | Too Many Requests | `CLIENT_ERROR` | RFC 6585 §4 | `NGX_HTTP_TOO_MANY_REQUESTS` |
| 431 | Request Header Fields Too Large | `CLIENT_ERROR` | RFC 6585 §5 | (none) |
| 451 | Unavailable For Legal Reasons | `CLIENT_ERROR \| CACHEABLE` | RFC 7725 §3 | (none) |

**Notes:**

- Codes 401 (Unauthorized) and 407 (Proxy Authentication Required) MUST be paired with a `WWW-Authenticate` or `Proxy-Authenticate` header per RFC 9110 § 11.6. The registry records the section reference; emitting these codes without the required header is a protocol violation that this API does NOT enforce (out of scope).
- Code 407 (Proxy Authentication Required) is NOT included in the registry; it is reserved for proxy-server-originated responses, which NGINX does not generate as a forward proxy in the typical reverse-proxy deployment. The `ngx_http_status_lines[]` wire table also omits 407 (renders as a null-string placeholder).

### NGINX-Specific 4xx Extensions

Six codes in the 4xx range are NGINX-specific and not registered with IANA. They carry both the `CLIENT_ERROR` and `NGINX_EXT` flag bits. The `rfc_section` field for these entries is the literal string `"nginx extension"`.

| Code | Reason Phrase (Registry) | Flags | RFC Section | Macro |
|---|---|---|---|---|
| 444 | Connection Closed Without Response | `CLIENT_ERROR \| NGINX_EXT` | nginx extension | `NGX_HTTP_CLOSE` |
| 494 | Request Header Too Large | `CLIENT_ERROR \| NGINX_EXT` | nginx extension | `NGX_HTTP_REQUEST_HEADER_TOO_LARGE` |
| 495 | SSL Certificate Error | `CLIENT_ERROR \| NGINX_EXT` | nginx extension | `NGX_HTTPS_CERT_ERROR` |
| 496 | SSL Certificate Required | `CLIENT_ERROR \| NGINX_EXT` | nginx extension | `NGX_HTTPS_NO_CERT` |
| 497 | HTTP Request Sent to HTTPS Port | `CLIENT_ERROR \| NGINX_EXT` | nginx extension | `NGX_HTTP_TO_HTTPS` |
| 499 | Client Closed Request | `CLIENT_ERROR \| NGINX_EXT` | nginx extension | `NGX_HTTP_CLIENT_CLOSED_REQUEST` |

**Note on `NGX_HTTP_NGINX_CODES`:** The macro `NGX_HTTP_NGINX_CODES` (value 494) is defined in `src/http/ngx_http_request.h` as a **threshold sentinel** for "first NGINX-extension code." It shares its numeric value with `NGX_HTTP_REQUEST_HEADER_TOO_LARGE`. The threshold sentinel is used in conditionals such as `if (status >= NGX_HTTP_NGINX_CODES)` to detect NGINX-extension codes. The actual emission macro for 494 is `NGX_HTTP_REQUEST_HEADER_TOO_LARGE`, listed in the table above.

**Semantics of each extension (preserved exactly from existing NGINX behavior):**

- **444 Connection Closed Without Response** — Used internally to instruct the server to return no information to the client and close the connection immediately. Emitted by the `return` directive in `nginx.conf`.
- **494 Request Header Too Large** — Expansion of 400 Bad Request; emitted when the client sends a request whose header line is too long. Also triggers keep-alive disable.
- **495 SSL Certificate Error** — Expansion of 400 Bad Request; emitted when the client has provided an invalid client certificate. Also triggers keep-alive disable, lingering-close disable, and TLS masquerade (wire emits 400; `$status` reports 495).
- **496 SSL Certificate Required** — Expansion of 400 Bad Request; emitted when a client certificate is required but not provided. Also triggers keep-alive disable, lingering-close disable, and TLS masquerade (wire emits 400; `$status` reports 496).
- **497 HTTP Request Sent to HTTPS Port** — Expansion of 400 Bad Request; emitted when the client has made an HTTP request to a port listening for HTTPS. Triggers keep-alive disable, lingering-close disable, and TLS masquerade (wire emits 400; `$status` reports 497).
- **499 Client Closed Request** — Used when the client has closed the connection before the server could send a response. Emitted only to access logs; never sent on the wire.

**TLS masquerade:** Codes 494, 495, 496, 497 emit wire status **400** to avoid leaking TLS-handshake-failure details to a potentially malicious HTTP client, while the access log's `$status` variable reports the original NGINX-specific code. This behavior is preserved byte-for-byte from pre-refactor NGINX per AAP § 0.2.2 and § 0.8.5.

### 5xx Server Error

Final responses indicating that the server is aware that it has erred or is incapable of performing the requested method. Per RFC 9110 § 15.6.

| Code | Reason Phrase (Registry) | Flags | RFC Section | Macro |
|---|---|---|---|---|
| 500 | Internal Server Error | `SERVER_ERROR` | RFC 9110 §15.6.1 | `NGX_HTTP_INTERNAL_SERVER_ERROR` |
| 501 | Not Implemented | `SERVER_ERROR \| CACHEABLE` | RFC 9110 §15.6.2 | `NGX_HTTP_NOT_IMPLEMENTED` |
| 502 | Bad Gateway | `SERVER_ERROR` | RFC 9110 §15.6.3 | `NGX_HTTP_BAD_GATEWAY` |
| 503 | Service Unavailable | `SERVER_ERROR` | RFC 9110 §15.6.4 | `NGX_HTTP_SERVICE_UNAVAILABLE` |
| 504 | Gateway Timeout | `SERVER_ERROR` | RFC 9110 §15.6.5 | `NGX_HTTP_GATEWAY_TIME_OUT` |
| 505 | HTTP Version Not Supported | `SERVER_ERROR` | RFC 9110 §15.6.6 | `NGX_HTTP_VERSION_NOT_SUPPORTED` |
| 506 | Variant Also Negotiates | `SERVER_ERROR` | RFC 2295 §8.1 | (none) |
| 507 | Insufficient Storage | `SERVER_ERROR` | RFC 4918 §11.5 | `NGX_HTTP_INSUFFICIENT_STORAGE` |
| 508 | Loop Detected | `SERVER_ERROR` | RFC 5842 §7.2 | (none) |
| 510 | Not Extended | `SERVER_ERROR` | RFC 2774 §7 | (none) |
| 511 | Network Authentication Required | `SERVER_ERROR` | RFC 6585 §6 | (none) |

**Registry total:** 4 (1xx) + 6 (2xx) + 7 (3xx) + 25 (4xx standard) + 6 (NGINX 4xx) + 11 (5xx) = **59 entries**.

**Note on count:** The registry includes 203 Non-Authoritative Information (RFC 9110 § 15.3.4) as a registry-only entry without a corresponding `NGX_HTTP_*` macro. Including this code yields 59 entries; excluding it (matching only the macros declared in `src/http/ngx_http_request.h`) yields 58. The registry initialization code in `src/http/ngx_http_status.c` is the source of truth for the precise count in any given build.


## Wire Phrase vs Registry Phrase Divergences

The HTTP/1.1 on-wire reason phrase emitted by NGINX is governed by the `ngx_http_status_lines[]` array in `src/http/ngx_http_header_filter_module.c`. This array is **preserved byte-for-byte** by the refactor per [decision D-008](../architecture/decision_log.md#architectural-decisions); the registry carries RFC 9110 canonical phrasing for future use without altering the wire format that existing clients see.

For 6 codes, the wire phrase differs materially from the RFC 9110 canonical phrase:

| Code | Wire Phrase (legacy NGINX) | Registry Phrase (RFC 9110) | Divergence Note |
|---|---|---|---|
| 302 | Moved Temporarily | Found | RFC 9110 § 15.4.3 standardized "Found" |
| 408 | Request Time-out | Request Timeout | RFC 9110 § 15.5.9 dropped the hyphen |
| 413 | Request Entity Too Large | Content Too Large | RFC 9110 § 15.5.14 renamed |
| 414 | Request-URI Too Large | URI Too Long | RFC 9110 § 15.5.15 shortened |
| 416 | Requested Range Not Satisfiable | Range Not Satisfiable | RFC 9110 § 15.5.17 shortened |
| 503 | Service Temporarily Unavailable | Service Unavailable | RFC 9110 § 15.6.4 standardized shorter form |

Two additional codes have minor stylistic variants (hyphenation/shortening) but the same semantic meaning:

| Code | Wire Phrase | Registry Phrase | Note |
|---|---|---|---|
| 405 | Not Allowed | Method Not Allowed | NGINX shortens; registry uses RFC canonical |
| 504 | Gateway Time-out | Gateway Timeout | Hyphenation difference only |

**Implications:**

- HTTP/1.1 clients see the wire phrase. The registry phrase is **never** emitted on the wire; it is purely an in-process value.
- `ngx_http_status_reason(302)` returns `"Found"` (RFC canonical). To obtain the wire phrase, do NOT use the API; consult `ngx_http_status_lines[]` directly or rely on `ngx_http_send_header()` which emits via the wire table.
- HTTP/2 and HTTP/3 emit only the numeric `:status` pseudo-header (via HPACK and QPACK respectively). Reason phrases are not on the wire for those protocols, so the divergence is unobservable to HTTP/2 and HTTP/3 clients.

**Why the divergence is intentional:** Pre-existing HTTP/1.1 clients may have parsed the legacy wire phrase as part of error-detection logic. Changing the wire phrase would be an observable behavior change and a regression from a strict backward-compatibility standpoint. The registry holds the canonical form for new code; the wire table holds the historical form for existing clients.

## Threading and Memory Semantics

### Thread Safety

NGINX uses a process-per-worker model; each worker is single-threaded by default. The registry-related API has the following safety properties:

| Function | Thread Safety |
|---|---|
| `ngx_http_status_set` | Per-worker re-entrant; no shared mutable state. Safe to call from any phase handler, filter, or event callback within a worker. |
| `ngx_http_status_validate` | Pure function; re-entrant in any context, including signal handlers (though that is not a typical NGINX usage). |
| `ngx_http_status_reason` | Reads immutable static `.rodata` registry; re-entrant. |
| `ngx_http_status_is_cacheable` | Reads immutable static `.rodata` registry; re-entrant. |
| `ngx_http_status_register` | NOT thread-safe; must be called only from the configuration parsing phase (single-threaded master process). Post-init calls return `NGX_ERROR`. |

The optional `--with-threads` AIO threadpool (used for large-file IO) does NOT touch the status code API; threadpool workers do not call `ngx_http_status_set` directly. If a future change introduces threadpool callers, the immutable-registry property still guarantees thread safety for the four read-side functions.

### Memory Model

Per [decision D-004](../architecture/decision_log.md#architectural-decisions), the registry is statically initialized at compile time via C aggregate initializers and stored in the `.rodata` section of the NGINX binary. Implications:

- **Zero per-worker heap allocation.** The registry is mapped read-only and shared across all worker processes via the kernel's copy-on-write page sharing.
- **Estimated `.rodata` footprint:** approximately 1.4 KB for the 58-entry array plus reason-phrase string literals. Per [AAP § 0.7.6 Memory Footprint](../architecture/decision_log.md#performance-impact), this exceeds the <1 KB per-worker target in raw size but is effectively 0 bytes per-worker incrementally because of page sharing.
- **No dangling pointers.** All `ngx_str_t` reason phrases reference compile-time string literals with static storage duration; they are never freed.
- **No initialization-order bugs.** Static initialization happens before `main()`; the registry is ready before any module's `init_module` callback runs.

### Forking Semantics

NGINX uses `fork(2)` (without `exec`) to spawn worker processes. After fork, the registry pages are mapped copy-on-write; because no worker writes to the registry, no copy is ever made and all workers share the same physical memory pages. This is the primary memory-efficiency mechanism for the registry and is part of the rationale for [decision D-004](../architecture/decision_log.md#architectural-decisions).

## Usage Examples

The following examples cover the common patterns encountered when migrating existing modules or developing new ones.

### Example 1: Setting a constant status (most common)

```c
static ngx_int_t
my_module_handler(ngx_http_request_t *r)
{
    /* Set 200 OK on a successful response.
     * For compile-time-constant in-range codes, the call is inlined to a
     * direct field write in permissive builds; cast to (void) to indicate
     * the return value is intentionally discarded. */
    (void) ngx_http_status_set(r, NGX_HTTP_OK);
    r->headers_out.content_length_n = body_len;

    return ngx_http_send_header(r);
}
```

### Example 2: Setting a runtime-variable status with defensive error handling (verbatim from AAP § 0.1.2)

```c
// OLD PATTERN (deprecated):
r->headers_out.status = NGX_HTTP_NOT_FOUND;

// NEW PATTERN (required):
if (ngx_http_status_set(r, 404) != NGX_OK) {
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                  "invalid status code: 404");
    return NGX_HTTP_INTERNAL_SERVER_ERROR;
}
```

### Example 3: Compile-time constant propagation (verbatim from AAP § 0.8.8)

```c
// Standard codes resolved at compile time for zero overhead
#define NGX_HTTP_OK 200
#define NGX_HTTP_NOT_FOUND 404

// Compiler optimizes: ngx_http_status_set(r, NGX_HTTP_OK)
// into direct assignment when validation disabled
```

### Example 4: Looking up a reason phrase

```c
ngx_uint_t        status = r->headers_out.status;
const ngx_str_t  *reason = ngx_http_status_reason(status);

ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
               "outgoing response: status=%ui reason=\"%V\"",
               status, reason);
```

### Example 5: Querying cacheability before file-cache eligibility

```c
if (r->cache && ngx_http_status_is_cacheable(r->headers_out.status)) {
    /* response is cache-eligible per RFC 9111 § 3 */
    r->cache->valid_sec = expiry;
    ngx_http_file_cache_update(r);
}
```

### Example 6: Upstream pass-through (special case)

```c
static ngx_int_t
my_upstream_handler(ngx_http_request_t *r)
{
    ngx_http_upstream_t  *u = r->upstream;

    /* When r->upstream is non-NULL, ngx_http_status_set bypasses strict
     * validation and assigns the upstream's code unconditionally. Even codes
     * outside 100-599 (e.g., a misbehaving backend emitting 999) reach the
     * wire verbatim, preserving pre-refactor pass-through semantics. */
    (void) ngx_http_status_set(r, u->headers_in.status_n);

    return ngx_http_send_header(r);
}
```

### Example 7: Registering a custom code during preconfiguration (rare, advanced)

```c
static ngx_int_t
my_module_preconfiguration(ngx_conf_t *cf)
{
    ngx_http_status_def_t  custom_code = {
        .code = 599,
        .reason = ngx_string("My Custom Server Error"),
        .flags = NGX_HTTP_STATUS_SERVER_ERROR | NGX_HTTP_STATUS_NGINX_EXT,
        .rfc_section = "vendor extension"
    };

    if (ngx_http_status_register(&custom_code) != NGX_OK) {
        ngx_log_error(NGX_LOG_EMERG, cf->log, 0,
                      "failed to register custom status code 599");
        return NGX_ERROR;
    }

    return NGX_OK;
}
```

**Note on the cast `(void)`:** The pattern `(void) ngx_http_status_set(...)` follows the NGINX coding convention for explicitly discarding a function's return value. It is appropriate when the call cannot fail (compile-time-constant in-range argument) or when the caller has determined that any failure should be silently absorbed. For runtime-variable arguments where strict-mode validation may reject the code, use the explicit `if` pattern from Example 2.

## See Also

- [`../migration/status_code_api.md`](../migration/status_code_api.md) — Migration guide for third-party module authors (the "how" of adopting the API)
- [`../architecture/decision_log.md`](../architecture/decision_log.md) — Architectural decisions D-001 through D-009 and bidirectional traceability matrix
- [`../architecture/status_code_refactor.md`](../architecture/status_code_refactor.md) — Mermaid before/after architecture diagrams (Fig-1 through Fig-5)
- [`../architecture/observability.md`](../architecture/observability.md) — Observability integration: log format, Grafana dashboard template, local verifiability checklist
- [`../../CODE_REVIEW.md`](../../CODE_REVIEW.md) — Six-phase segmented PR review artifact
- [RFC 9110](https://www.rfc-editor.org/rfc/rfc9110.html) — HTTP Semantics (the normative source of standard reason phrases)
- [RFC 9110 § 15](https://www.rfc-editor.org/rfc/rfc9110.html#name-status-codes) — HTTP Status Codes specification
- [RFC 9111](https://www.rfc-editor.org/rfc/rfc9111.html) — HTTP Caching (the normative source for the `NGX_HTTP_STATUS_CACHEABLE` semantics)
- [IANA HTTP Status Code Registry](https://www.iana.org/assignments/http-status-codes/http-status-codes.xml) — Authoritative registered-codes list
- [NGINX development guide](https://nginx.org/en/docs/dev/development_guide.html) — Coding conventions for module authors
- Source files:
  - `src/http/ngx_http_status.h` — Type definitions and flag-bit constants
  - `src/http/ngx_http_status.c` — Registry array and API implementations
  - `src/http/ngx_http.h` — Public function prototypes (transitively included by every HTTP module)
  - `src/http/ngx_http_request.h` — `#define NGX_HTTP_*` macros (preserved byte-for-byte; lines 74–145)
  - `src/http/ngx_http_header_filter_module.c` — `ngx_http_status_lines[]` wire table (preserved byte-for-byte)

