# HTTP Status Code Registry API Reference

**Version:** 1.0  
**Last Updated:** January 2026  
**Compliance:** RFC 9110 HTTP Semantics

---

## Table of Contents

1. [Overview](#overview)
2. [API Function Reference](#api-function-reference)
   - [ngx_http_status_set()](#ngx_http_status_set)
   - [ngx_http_status_validate()](#ngx_http_status_validate)
   - [ngx_http_status_reason()](#ngx_http_status_reason)
   - [ngx_http_status_is_cacheable()](#ngx_http_status_is_cacheable)
3. [Status Code Flags](#status-code-flags)
4. [Registry Data Model](#registry-data-model)
5. [Supported Status Codes](#supported-status-codes)
6. [NGINX-Specific Status Codes](#nginx-specific-status-codes)
7. [Build Configuration](#build-configuration)
8. [Migration Guide](#migration-guide)
9. [See Also](#see-also)

---

## Overview

### Purpose

The HTTP Status Code Registry API provides a centralized, unified interface for managing HTTP response status codes within the NGINX HTTP subsystem. This API replaces the scattered `#define` macro-based approach with a structured registry containing metadata for each status code, enabling:

- **RFC 9110 Compliance Validation:** Validates status codes against the HTTP Semantics specification
- **Centralized Status Code Management:** Single source of truth for all HTTP status code definitions
- **Enhanced Metadata:** Access to reason phrases, cacheability flags, and RFC references
- **Backward Compatibility:** Existing `NGX_HTTP_*` macros remain functional

### Architecture Summary

The Status Code Registry implements a centralized registry pattern that transforms NGINX's HTTP status handling:

```
Previous Architecture:                  New Architecture:
┌─────────────────────────┐            ┌─────────────────────────┐
│ Scattered #define macros│            │ Centralized Registry    │
│ in ngx_http_request.h   │    →       │ ngx_http_status_def_t   │
└─────────────────────────┘            │ static array            │
          │                            └─────────────────────────┘
          ▼                                      │
┌─────────────────────────┐                      ▼
│ Direct assignment       │            ┌─────────────────────────┐
│ r->headers_out.status   │    →       │ Unified API Layer       │
│ = NGX_HTTP_OK           │            │ ngx_http_status_set()   │
└─────────────────────────┘            │ ngx_http_status_reason()│
                                       └─────────────────────────┘
```

### RFC 9110 Compliance

This API implements validation per [RFC 9110 Section 15](https://www.rfc-editor.org/rfc/rfc9110#section-15):

- **Valid Range:** Status codes must be three-digit integers in the range 100-599
- **Status Classes:**
  - 1xx (Informational): Interim responses
  - 2xx (Successful): Request successfully received
  - 3xx (Redirection): Further action required
  - 4xx (Client Error): Request contains errors
  - 5xx (Server Error): Server failed to fulfill request
- **Reason Phrases:** Human-readable text associated with each status code

---

## API Function Reference

### ngx_http_status_set()

Sets the response status code for an HTTP request with optional RFC 9110 validation.

#### Synopsis

```c
ngx_int_t ngx_http_status_set(ngx_http_request_t *r, ngx_uint_t status);
```

#### Description

This function sets the HTTP response status code for the given request. When strict validation mode is enabled (via `--with-http_status_validation`), the function validates that the status code falls within the RFC 9110 valid range (100-599) before assignment.

This is the **canonical interface** for setting HTTP response status codes and should be used instead of direct assignment to `r->headers_out.status`.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `r` | `ngx_http_request_t *` | Pointer to the request structure representing the current HTTP request. Must not be NULL. |
| `status` | `ngx_uint_t` | HTTP status code to set. Valid range: 100-599 per RFC 9110. |

#### Return Value

| Value | Condition |
|-------|-----------|
| `NGX_OK` | Status code was successfully set |
| `NGX_ERROR` | Validation failed (strict mode only) - status code outside valid range |

#### Error Handling

When validation fails (in strict mode):

1. The function logs an error at `NGX_LOG_ERR` level with request context
2. The status code is **not** modified
3. The calling module should fall back to `NGX_HTTP_INTERNAL_SERVER_ERROR` (500)

**Log Format:**
```
"http status set: %ui (invalid)"
```

#### Performance

- **Overhead:** Less than 10 CPU cycles compared to direct `r->headers_out.status` assignment
- **Complexity:** O(1) - single range check and assignment

#### Example Usage

**Basic Usage:**

```c
/* Setting a successful response status */
ngx_http_status_set(r, NGX_HTTP_OK);
```

**With Error Handling (Recommended):**

```c
if (ngx_http_status_set(r, 404) != NGX_OK) {
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                  "invalid status code: 404");
    return NGX_HTTP_INTERNAL_SERVER_ERROR;
}
```

**Setting Custom Status Codes:**

```c
/* Setting a 429 Too Many Requests status */
if (ngx_http_status_set(r, NGX_HTTP_TOO_MANY_REQUESTS) != NGX_OK) {
    return NGX_HTTP_INTERNAL_SERVER_ERROR;
}
```

#### Migration Pattern

```c
/* OLD PATTERN (deprecated): */
r->headers_out.status = NGX_HTTP_NOT_FOUND;

/* NEW PATTERN (recommended): */
if (ngx_http_status_set(r, NGX_HTTP_NOT_FOUND) != NGX_OK) {
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                  "failed to set status code");
    return NGX_HTTP_INTERNAL_SERVER_ERROR;
}
```

#### Notes

- The existing `NGX_HTTP_*` macro constants can still be used as the `status` parameter
- Direct assignment to `r->headers_out.status` remains functional for backward compatibility but is deprecated
- Upstream/proxied responses bypass strict validation to preserve backend status codes

---

### ngx_http_status_validate()

Validates that a status code is within the RFC 9110 valid range.

#### Synopsis

```c
ngx_int_t ngx_http_status_validate(ngx_uint_t status);
```

#### Description

This function validates whether the provided status code falls within the valid HTTP status code range as defined by RFC 9110 (100-599). Unlike `ngx_http_status_set()`, this function does not require a request context, making it suitable for:

- Configuration-time validation
- Pre-flight status code checks
- Module initialization validation

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `status` | `ngx_uint_t` | HTTP status code to validate |

#### Return Value

| Value | Condition |
|-------|-----------|
| `NGX_OK` | Status code is valid (100-599) |
| `NGX_ERROR` | Status code is outside valid range |

#### Performance

- **Complexity:** O(1) - single range comparison
- **Overhead:** Negligible (2-3 CPU instructions)

#### Example Usage

**Configuration Validation:**

```c
/* Validating status code during configuration parsing */
if (ngx_http_status_validate(conf->custom_status) != NGX_OK) {
    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                       "invalid status code \"%ui\"", conf->custom_status);
    return NGX_CONF_ERROR;
}
```

**Pre-Assignment Check:**

```c
ngx_uint_t status = calculate_status_code();

if (ngx_http_status_validate(status) == NGX_OK) {
    ngx_http_status_set(r, status);
} else {
    ngx_http_status_set(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
}
```

#### RFC Reference

Per RFC 9110 Section 15:
> "The status-code element is a 3-digit integer code describing the result of the server's attempt to understand and satisfy the client's corresponding request."

Valid status code classes:
- 1xx: Informational (100-199)
- 2xx: Successful (200-299)
- 3xx: Redirection (300-399)
- 4xx: Client Error (400-499)
- 5xx: Server Error (500-599)

---

### ngx_http_status_reason()

Retrieves the standard reason phrase for an HTTP status code.

#### Synopsis

```c
const ngx_str_t *ngx_http_status_reason(ngx_uint_t status);
```

#### Description

This function performs a lookup in the centralized status code registry and returns the standard reason phrase associated with the given HTTP status code. Reason phrases are defined per RFC 9110 Section 15.

The returned string is read-only and stored in the registry's static data section.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `status` | `ngx_uint_t` | HTTP status code to look up (100-599) |

#### Return Value

| Value | Condition |
|-------|-----------|
| `const ngx_str_t *` | Pointer to the reason phrase for known status codes |
| `NULL` | Status code is unknown or not registered |

#### Performance

- **Complexity:** O(1) - computed array offset lookup
- **Memory:** No allocation; returns pointer to static data

#### Example Usage

**Basic Lookup:**

```c
const ngx_str_t *reason = ngx_http_status_reason(NGX_HTTP_OK);
if (reason != NULL) {
    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, log, 0,
                   "status: 200 %V", reason);
}
```

**Building Status Line:**

```c
const ngx_str_t *reason = ngx_http_status_reason(r->headers_out.status);
if (reason == NULL) {
    /* Unknown status code - use generic reason */
    reason = &ngx_http_unknown_status;
}

/* reason->data contains "OK", "Not Found", etc. */
```

**HTTP/2 HPACK Encoding:**

```c
const ngx_str_t *reason = ngx_http_status_reason(status);
if (reason != NULL) {
    /* Use reason phrase for HPACK encoding */
    len = reason->len;
    p = ngx_cpymem(p, reason->data, len);
}
```

#### RFC Reference

RFC 9110 Section 15.1 states:
> "The reason-phrase element exists for the sole purpose of providing a textual description associated with the numeric status code, mostly out of deference to earlier Internet application protocols that were more frequently used with interactive text clients."

#### Reason Phrase Examples

| Status Code | Reason Phrase |
|-------------|---------------|
| 200 | OK |
| 201 | Created |
| 301 | Moved Permanently |
| 304 | Not Modified |
| 400 | Bad Request |
| 401 | Unauthorized |
| 403 | Forbidden |
| 404 | Not Found |
| 500 | Internal Server Error |
| 502 | Bad Gateway |
| 503 | Service Unavailable |

---

### ngx_http_status_is_cacheable()

Checks if a status code indicates a cacheable response per RFC 9110.

#### Synopsis

```c
ngx_int_t ngx_http_status_is_cacheable(ngx_uint_t status);
```

#### Description

This function determines whether responses with the given status code are cacheable by default, as defined by RFC 9110 Section 15.1. Cacheability is a response property that indicates whether a response can be stored by caches for reuse.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `status` | `ngx_uint_t` | HTTP status code to check |

#### Return Value

| Value | Condition |
|-------|-----------|
| `1` | Status code indicates a cacheable response |
| `0` | Status code is not cacheable by default |

#### Default Cacheable Status Codes

Per RFC 9110 Section 15.1, the following status codes are cacheable by default:

| Status Code | Reason Phrase | Cacheability |
|-------------|---------------|--------------|
| 200 | OK | Cacheable |
| 203 | Non-Authoritative Information | Cacheable |
| 204 | No Content | Cacheable |
| 206 | Partial Content | Cacheable |
| 300 | Multiple Choices | Cacheable |
| 301 | Moved Permanently | Cacheable |
| 308 | Permanent Redirect | Cacheable |
| 404 | Not Found | Cacheable |
| 405 | Method Not Allowed | Cacheable |
| 410 | Gone | Cacheable |
| 414 | URI Too Long | Cacheable |
| 501 | Not Implemented | Cacheable |

#### Performance

- **Complexity:** O(1) - flag lookup from registry
- **Overhead:** Single bitwise AND operation

#### Example Usage

**Cache Decision Logic:**

```c
if (ngx_http_status_is_cacheable(r->headers_out.status)) {
    /* Response can be cached */
    ctx->cacheable = 1;
} else {
    /* Response requires explicit Cache-Control for caching */
    ctx->cacheable = 0;
}
```

**Upstream Cache Validation:**

```c
ngx_uint_t status = u->headers_in.status_n;

if (ngx_http_status_is_cacheable(status)) {
    /* Store in upstream cache */
    ngx_http_upstream_cache_store(r, u);
}
```

#### RFC Reference

RFC 9110 Section 15.1 defines response cacheability:
> "Responses to the GET and HEAD methods are cacheable by default when a 'freshness lifetime' can be calculated. The following status codes are defined as cacheable by default..."

#### Notes

- Cacheability determined by this function is the **default** behavior
- Cache-Control directives can override default cacheability
- Private or authenticated responses may have different caching rules

---

## Status Code Flags

The registry defines classification flags for categorizing status codes. These flags are defined in `src/http/ngx_http_request.h`:

### Flag Definitions

| Flag | Value | Description |
|------|-------|-------------|
| `NGX_HTTP_STATUS_CACHEABLE` | `0x0001` | Response is cacheable by default per RFC 9110 |
| `NGX_HTTP_STATUS_CLIENT_ERROR` | `0x0002` | Status code in 4xx range (client error) |
| `NGX_HTTP_STATUS_SERVER_ERROR` | `0x0004` | Status code in 5xx range (server error) |
| `NGX_HTTP_STATUS_INFORMATIONAL` | `0x0008` | Status code in 1xx range (informational) |

### Flag Usage

```c
/* Example: Check if status is a client error */
if (status_def->flags & NGX_HTTP_STATUS_CLIENT_ERROR) {
    /* Handle 4xx error */
}

/* Example: Check cacheability and error class */
if ((status_def->flags & NGX_HTTP_STATUS_CACHEABLE)
    && !(status_def->flags & NGX_HTTP_STATUS_SERVER_ERROR))
{
    /* Cacheable non-server-error response */
}
```

### Flag Combinations by Status Class

| Status Class | Typical Flags |
|--------------|---------------|
| 1xx Informational | `NGX_HTTP_STATUS_INFORMATIONAL` |
| 2xx Success | `NGX_HTTP_STATUS_CACHEABLE` (for 200, 204, 206) |
| 3xx Redirection | `NGX_HTTP_STATUS_CACHEABLE` (for 301, 304, 308) |
| 4xx Client Error | `NGX_HTTP_STATUS_CLIENT_ERROR`, `NGX_HTTP_STATUS_CACHEABLE` (for 404, 405, 410, 414) |
| 5xx Server Error | `NGX_HTTP_STATUS_SERVER_ERROR`, `NGX_HTTP_STATUS_CACHEABLE` (for 501 only) |

---

## Registry Data Model

### ngx_http_status_def_t Structure

The centralized registry uses the following structure to store status code metadata:

```c
typedef struct {
    ngx_uint_t    code;          /* HTTP status code (100-599) */
    ngx_str_t     reason;        /* Reason phrase per RFC 9110 */
    ngx_uint_t    flags;         /* Classification flags */
    const char   *rfc_section;   /* RFC 9110 section reference */
} ngx_http_status_def_t;
```

### Field Descriptions

| Field | Type | Description |
|-------|------|-------------|
| `code` | `ngx_uint_t` | Numeric HTTP status code (100-599) |
| `reason` | `ngx_str_t` | Human-readable reason phrase (e.g., "OK", "Not Found") |
| `flags` | `ngx_uint_t` | Bitwise OR of `NGX_HTTP_STATUS_*` flags |
| `rfc_section` | `const char *` | Reference to RFC 9110 section (e.g., "15.3.5") |

### Registry Characteristics

- **Storage:** Static array in `.rodata` section (read-only)
- **Initialization:** Compile-time (zero runtime allocation)
- **Thread Safety:** Immutable after initialization
- **Lookup Performance:** O(1) via computed array offset
- **Memory Footprint:** Less than 1KB total

### Example Registry Entry

```c
/* 404 Not Found entry */
{
    404,
    ngx_string("Not Found"),
    NGX_HTTP_STATUS_CLIENT_ERROR | NGX_HTTP_STATUS_CACHEABLE,
    "15.5.5"
}
```

---

## Supported Status Codes

### 1xx Informational

Informational responses indicate that the request was received and understood. These are interim responses sent before the final response.

| Code | Constant | Reason Phrase | RFC 9110 Section |
|------|----------|---------------|------------------|
| 100 | `NGX_HTTP_CONTINUE` | Continue | 15.2.1 |
| 101 | `NGX_HTTP_SWITCHING_PROTOCOLS` | Switching Protocols | 15.2.2 |
| 102 | `NGX_HTTP_PROCESSING` | Processing | - (WebDAV) |
| 103 | `NGX_HTTP_EARLY_HINTS` | Early Hints | - (RFC 8297) |

### 2xx Success

Success responses indicate that the request was successfully received, understood, and accepted.

| Code | Constant | Reason Phrase | Cacheable | RFC 9110 Section |
|------|----------|---------------|-----------|------------------|
| 200 | `NGX_HTTP_OK` | OK | Yes | 15.3.1 |
| 201 | `NGX_HTTP_CREATED` | Created | No | 15.3.2 |
| 202 | `NGX_HTTP_ACCEPTED` | Accepted | No | 15.3.3 |
| 204 | `NGX_HTTP_NO_CONTENT` | No Content | Yes | 15.3.5 |
| 206 | `NGX_HTTP_PARTIAL_CONTENT` | Partial Content | Yes | 15.3.7 |

### 3xx Redirection

Redirection responses indicate that further action needs to be taken to complete the request.

| Code | Constant | Reason Phrase | Cacheable | RFC 9110 Section |
|------|----------|---------------|-----------|------------------|
| 300 | `NGX_HTTP_SPECIAL_RESPONSE` | Multiple Choices | Yes | 15.4.1 |
| 301 | `NGX_HTTP_MOVED_PERMANENTLY` | Moved Permanently | Yes | 15.4.2 |
| 302 | `NGX_HTTP_MOVED_TEMPORARILY` | Found | No | 15.4.3 |
| 303 | `NGX_HTTP_SEE_OTHER` | See Other | No | 15.4.4 |
| 304 | `NGX_HTTP_NOT_MODIFIED` | Not Modified | Yes | 15.4.5 |
| 307 | `NGX_HTTP_TEMPORARY_REDIRECT` | Temporary Redirect | No | 15.4.8 |
| 308 | `NGX_HTTP_PERMANENT_REDIRECT` | Permanent Redirect | Yes | 15.4.9 |

### 4xx Client Error

Client error responses indicate that the request contains bad syntax or cannot be fulfilled.

| Code | Constant | Reason Phrase | Cacheable | RFC 9110 Section |
|------|----------|---------------|-----------|------------------|
| 400 | `NGX_HTTP_BAD_REQUEST` | Bad Request | No | 15.5.1 |
| 401 | `NGX_HTTP_UNAUTHORIZED` | Unauthorized | No | 15.5.2 |
| 403 | `NGX_HTTP_FORBIDDEN` | Forbidden | No | 15.5.4 |
| 404 | `NGX_HTTP_NOT_FOUND` | Not Found | Yes | 15.5.5 |
| 405 | `NGX_HTTP_NOT_ALLOWED` | Method Not Allowed | Yes | 15.5.6 |
| 408 | `NGX_HTTP_REQUEST_TIME_OUT` | Request Timeout | No | 15.5.9 |
| 409 | `NGX_HTTP_CONFLICT` | Conflict | No | 15.5.10 |
| 410 | - | Gone | Yes | 15.5.11 |
| 411 | `NGX_HTTP_LENGTH_REQUIRED` | Length Required | No | 15.5.12 |
| 412 | `NGX_HTTP_PRECONDITION_FAILED` | Precondition Failed | No | 15.5.13 |
| 413 | `NGX_HTTP_REQUEST_ENTITY_TOO_LARGE` | Content Too Large | No | 15.5.14 |
| 414 | `NGX_HTTP_REQUEST_URI_TOO_LARGE` | URI Too Long | Yes | 15.5.15 |
| 415 | `NGX_HTTP_UNSUPPORTED_MEDIA_TYPE` | Unsupported Media Type | No | 15.5.16 |
| 416 | `NGX_HTTP_RANGE_NOT_SATISFIABLE` | Range Not Satisfiable | No | 15.5.17 |
| 421 | `NGX_HTTP_MISDIRECTED_REQUEST` | Misdirected Request | No | 15.5.20 |
| 429 | `NGX_HTTP_TOO_MANY_REQUESTS` | Too Many Requests | No | - (RFC 6585) |

### 5xx Server Error

Server error responses indicate that the server failed to fulfill a valid request.

| Code | Constant | Reason Phrase | Cacheable | RFC 9110 Section |
|------|----------|---------------|-----------|------------------|
| 500 | `NGX_HTTP_INTERNAL_SERVER_ERROR` | Internal Server Error | No | 15.6.1 |
| 501 | `NGX_HTTP_NOT_IMPLEMENTED` | Not Implemented | Yes | 15.6.2 |
| 502 | `NGX_HTTP_BAD_GATEWAY` | Bad Gateway | No | 15.6.3 |
| 503 | `NGX_HTTP_SERVICE_UNAVAILABLE` | Service Unavailable | No | 15.6.4 |
| 504 | `NGX_HTTP_GATEWAY_TIME_OUT` | Gateway Timeout | No | 15.6.5 |
| 505 | `NGX_HTTP_VERSION_NOT_SUPPORTED` | HTTP Version Not Supported | No | 15.6.6 |
| 507 | `NGX_HTTP_INSUFFICIENT_STORAGE` | Insufficient Storage | No | - (WebDAV) |

---

## NGINX-Specific Status Codes

NGINX defines several custom status codes for internal use that are not part of the standard HTTP specification.

### NGX_HTTP_CLOSE (444)

**Purpose:** Close the connection without sending any response.

**Constant:** `NGX_HTTP_CLOSE`  
**Value:** `444`

**Use Case:** Silently drop connections for malicious requests or when no response is appropriate.

```c
/* Close connection without response */
if (is_malicious_request(r)) {
    return NGX_HTTP_CLOSE;
}
```

**Behavior:**
- Connection is closed immediately
- No HTTP response headers or body are sent
- Client receives a connection reset
- Useful for blocking unwanted traffic

### NGX_HTTP_REQUEST_HEADER_TOO_LARGE (494)

**Purpose:** Indicate that request headers exceeded configured limits.

**Constant:** `NGX_HTTP_REQUEST_HEADER_TOO_LARGE`  
**Value:** `494`

**Use Case:** Returned when the request header size exceeds `large_client_header_buffers`.

```c
/* When header parsing exceeds buffer size */
if (header_size > conf->large_client_header_buffers_size) {
    return NGX_HTTP_REQUEST_HEADER_TOO_LARGE;
}
```

### NGX_HTTP_TO_HTTPS (497)

**Purpose:** Indicate that an HTTP request was sent to an HTTPS port.

**Constant:** `NGX_HTTP_TO_HTTPS`  
**Value:** `497`

**Use Case:** Distinguishes HTTP-to-HTTPS redirects from standard 4xx errors in error_page handling.

```c
/* Used internally when HTTP request arrives on HTTPS port */
/* Can trigger error_page 497 for custom handling */
```

**Configuration Example:**
```nginx
error_page 497 =301 https://$host$request_uri;
```

### NGX_HTTP_CLIENT_CLOSED_REQUEST (499)

**Purpose:** Log when a client closed the connection before receiving a complete response.

**Constant:** `NGX_HTTP_CLIENT_CLOSED_REQUEST`  
**Value:** `499`

**Use Case:** Logging and debugging client connection behavior.

```c
/* Logged when client disconnects prematurely */
/* Appears in access logs as status 499 */
```

**Typical Causes:**
- Client timeout exceeded
- User navigated away
- Network interruption
- Client-side application crash

### NGINX Extension Code Summary

| Code | Constant | Description | Logged |
|------|----------|-------------|--------|
| 444 | `NGX_HTTP_CLOSE` | Close connection without response | No |
| 494 | `NGX_HTTP_REQUEST_HEADER_TOO_LARGE` | Request header fields too large | Yes |
| 495 | `NGX_HTTPS_CERT_ERROR` | SSL certificate error | Yes |
| 496 | `NGX_HTTPS_NO_CERT` | Client certificate required | Yes |
| 497 | `NGX_HTTP_TO_HTTPS` | HTTP to HTTPS redirect required | Yes |
| 499 | `NGX_HTTP_CLIENT_CLOSED_REQUEST` | Client closed connection | Yes |

---

## Build Configuration

### Validation Mode Configure Flag

The HTTP Status Code Registry supports two validation modes controlled by a compile-time flag.

#### --with-http_status_validation

**Purpose:** Enable strict RFC 9110 validation mode.

**Usage:**
```bash
./auto/configure --with-http_status_validation
make
make install
```

#### Default Mode (Permissive)

When `--with-http_status_validation` is **not** specified:

| Behavior | Description |
|----------|-------------|
| Range Check | Accepts status codes 100-599 |
| Invalid Codes | Logged at `NGX_LOG_DEBUG` level |
| Assignment | Always succeeds for valid range |
| Backward Compatibility | Full compatibility with existing behavior |

#### Strict Mode (RFC 9110)

When `--with-http_status_validation` **is** specified:

| Behavior | Description |
|----------|-------------|
| Range Check | Enforces 100-599 range strictly |
| Invalid Codes | Logged at `NGX_LOG_WARN` (production) or `NGX_LOG_ERR` (debug) |
| Assignment | Fails for codes outside valid range |
| Validation | Returns `NGX_ERROR` for invalid status codes |

### Build System Files

| File | Purpose |
|------|---------|
| `auto/options` | Defines `HTTP_STATUS_VALIDATION=NO` default |
| `auto/modules` | Handles conditional compilation |

### Configuration Detection

To check if validation mode is enabled:

```c
#if (NGX_HTTP_STATUS_VALIDATION)
    /* Strict validation enabled */
#else
    /* Permissive mode (default) */
#endif
```

---

## Migration Guide

### Quick Reference

| Old Pattern | New Pattern |
|-------------|-------------|
| `r->headers_out.status = NGX_HTTP_OK;` | `ngx_http_status_set(r, NGX_HTTP_OK);` |
| `r->headers_out.status = 404;` | `ngx_http_status_set(r, 404);` |
| Status comparisons | No change required |
| `ngx_http_status_lines[index]` | `ngx_http_status_reason(status)` |

### Detailed Migration Steps

#### Step 1: Identify Status Assignments

Search your module for direct status assignments:

```bash
grep -n "r->headers_out.status\s*=" your_module.c
```

#### Step 2: Replace with API Calls

**Simple Assignment:**

```c
/* Before */
r->headers_out.status = NGX_HTTP_OK;

/* After */
ngx_http_status_set(r, NGX_HTTP_OK);
```

**With Error Handling (Recommended):**

```c
/* Before */
r->headers_out.status = status_code;
return ngx_http_send_header(r);

/* After */
if (ngx_http_status_set(r, status_code) != NGX_OK) {
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                  "invalid status code: %ui", status_code);
    return NGX_HTTP_INTERNAL_SERVER_ERROR;
}
return ngx_http_send_header(r);
```

#### Step 3: Update Reason Phrase Lookups

**Before:**

```c
ngx_str_t *line;
line = &ngx_http_status_lines[r->headers_out.status - 200];
```

**After:**

```c
const ngx_str_t *reason;
reason = ngx_http_status_reason(r->headers_out.status);
if (reason == NULL) {
    /* Handle unknown status code */
}
```

#### Step 4: Preserve Comparisons

Status code **comparisons** do not need changes:

```c
/* These patterns remain unchanged */
if (r->headers_out.status == NGX_HTTP_OK) { ... }
if (r->headers_out.status >= NGX_HTTP_BAD_REQUEST) { ... }
if (r->headers_out.status < NGX_HTTP_SPECIAL_RESPONSE) { ... }
```

### Upstream Module Considerations

For modules that pass through upstream responses:

```c
/* Upstream status codes bypass validation */
/* The API handles this automatically when r->upstream is set */
ngx_http_status_set(r, u->headers_in.status_n);
```

### Testing After Migration

1. **Compilation Test:**
   ```bash
   make clean && make
   ```

2. **Regression Test:**
   ```bash
   prove -r t/
   ```

3. **Performance Test:**
   ```bash
   wrk -t4 -c100 -d30s http://localhost/
   ```

---

## See Also

### Related Documentation

- [Migration Guide](../migration/status_code_api.md) - Detailed migration instructions for module developers
- [NGINX Development Guide](https://nginx.org/en/docs/dev/development_guide.html) - Module development reference

### Source Files

| File | Description |
|------|-------------|
| `src/http/ngx_http.h` | API function declarations |
| `src/http/ngx_http_request.h` | Status code constants and flag definitions |
| `src/http/ngx_http_request.c` | Registry implementation and API functions |

### Standards References

| Standard | Title | Relevance |
|----------|-------|-----------|
| [RFC 9110](https://www.rfc-editor.org/rfc/rfc9110) | HTTP Semantics | Section 15 - Status Codes |
| [RFC 9111](https://www.rfc-editor.org/rfc/rfc9111) | HTTP Caching | Cacheability rules |
| [RFC 9112](https://www.rfc-editor.org/rfc/rfc9112) | HTTP/1.1 | Status line format |
| [RFC 9113](https://www.rfc-editor.org/rfc/rfc9113) | HTTP/2 | HPACK status encoding |
| [RFC 9114](https://www.rfc-editor.org/rfc/rfc9114) | HTTP/3 | QPACK status encoding |
| [RFC 6585](https://www.rfc-editor.org/rfc/rfc6585) | Additional HTTP Status Codes | 429 Too Many Requests |
| [RFC 8297](https://www.rfc-editor.org/rfc/rfc8297) | Early Hints | 103 Early Hints |

### IANA Registry

- [HTTP Status Codes Registry](https://www.iana.org/assignments/http-status-codes/http-status-codes.xhtml) - Official IANA status code assignments

---

*This document is part of the NGINX HTTP Status Code Registry API implementation.*
