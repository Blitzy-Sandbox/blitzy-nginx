# HTTP Status Code API Migration Guide

This document provides comprehensive guidance for third-party NGINX module
developers transitioning from direct `r->headers_out.status` assignments to
the new centralized `ngx_http_status_set()` API.

**Version:** 1.0  
**Last Updated:** January 2026  
**Applies to:** NGINX 1.27.0+  

---

## Table of Contents

1. [Introduction](#introduction)
2. [Migration Overview](#migration-overview)
3. [Step-by-Step Migration Instructions](#step-by-step-migration-instructions)
4. [Before/After Code Patterns](#beforeafter-code-patterns)
5. [Error Handling Patterns](#error-handling-patterns)
6. [Common Status Code Examples](#common-status-code-examples)
7. [Backward Compatibility](#backward-compatibility)
8. [API Reference Summary](#api-reference-summary)
9. [Special Considerations](#special-considerations)
10. [Changelog Reference](#changelog-reference)

---

## Introduction

### Overview of the Refactoring

Prior to this refactoring, HTTP status codes in NGINX were managed through
scattered `#define` macros in `src/http/ngx_http_request.h` and direct
assignment to the `r->headers_out.status` field. While functional, this
approach had several limitations:

- No centralized validation of status codes
- Reason phrases stored in a separate static array
- No metadata about status code properties (cacheability, error class)
- Difficult to enforce RFC 9110 compliance

The new architecture introduces a **centralized status code registry** with
a **unified API layer** that provides:

- Single source of truth for all HTTP status code definitions
- Built-in validation against RFC 9110 HTTP Semantics specification
- Integrated reason phrase lookup
- Status code metadata (cacheability flags, error classification)
- Consistent error handling and logging

### Unified API Layer

The refactoring introduces four primary API functions:

| Function | Purpose |
|----------|---------|
| `ngx_http_status_set()` | Set response status code with validation |
| `ngx_http_status_validate()` | Validate a status code without setting it |
| `ngx_http_status_reason()` | Retrieve reason phrase for a status code |
| `ngx_http_status_is_cacheable()` | Check if a status code is cacheable by default |

### RFC 9110 Compliance

RFC 9110 (HTTP Semantics) defines HTTP status codes as three-digit integers
in the range 100-599. The new API provides optional strict validation mode
that enforces:

- Status codes must be in the valid range (100-599)
- Status code class determined by first digit (1xx-5xx)
- Proper handling of informational (1xx) responses
- Cacheability rules per RFC 9111

---

## Migration Overview

### New API Functions

The following functions are now available in `<ngx_http.h>`:

```c
/* Set response status code with validation */
ngx_int_t ngx_http_status_set(ngx_http_request_t *r, ngx_uint_t status);

/* Validate a status code (standalone check) */
ngx_int_t ngx_http_status_validate(ngx_uint_t status);

/* Get reason phrase for a status code */
const ngx_str_t *ngx_http_status_reason(ngx_uint_t status);

/* Check if status code is cacheable by default (per RFC 9111) */
ngx_int_t ngx_http_status_is_cacheable(ngx_uint_t status);
```

### Backward Compatibility Guarantees

**All existing code continues to work without modification:**

- Existing `NGX_HTTP_*` macro constants remain fully functional
- Direct assignment to `r->headers_out.status` still compiles and works
- Status comparison patterns (`r->headers_out.status == NGX_HTTP_OK`) unchanged
- Third-party modules compiled against older NGINX headers remain compatible

**Migration is recommended but not required.** The direct assignment pattern
is deprecated for new development but will not be removed.

### Status Code Flag Definitions

New flag constants are available in `<ngx_http_request.h>`:

```c
#define NGX_HTTP_STATUS_CACHEABLE       0x0001  /* Cacheable by default */
#define NGX_HTTP_STATUS_CLIENT_ERROR    0x0002  /* 4xx client error */
#define NGX_HTTP_STATUS_SERVER_ERROR    0x0004  /* 5xx server error */
#define NGX_HTTP_STATUS_INFORMATIONAL   0x0008  /* 1xx informational */
```

---

## Step-by-Step Migration Instructions

### Step 1: Identify Status Code Assignments

Search your module source code for patterns matching:

```bash
grep -n "r->headers_out.status\s*=" your_module.c
```

Or more specifically for direct macro assignments:

```bash
grep -n "r->headers_out.status\s*=\s*NGX_HTTP_" your_module.c
```

### Step 2: Convert Direct Assignments

Replace each direct assignment with the `ngx_http_status_set()` function:

**Before (deprecated):**
```c
r->headers_out.status = NGX_HTTP_OK;
```

**After (recommended):**
```c
ngx_http_status_set(r, NGX_HTTP_OK);
```

### Step 3: Add Error Handling

The `ngx_http_status_set()` function returns `NGX_OK` on success or
`NGX_ERROR` if the status code is invalid (outside range 100-599 in strict
mode).

**Minimal error handling:**
```c
if (ngx_http_status_set(r, status_code) != NGX_OK) {
    return NGX_HTTP_INTERNAL_SERVER_ERROR;
}
```

**Comprehensive error handling with logging:**
```c
if (ngx_http_status_set(r, status_code) != NGX_OK) {
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                  "invalid status code: %ui", status_code);
    return NGX_HTTP_INTERNAL_SERVER_ERROR;
}
```

### Step 4: Update Reason Phrase Lookups

If your module accesses the `ngx_http_status_lines[]` array directly,
replace with `ngx_http_status_reason()`:

**Before (deprecated):**
```c
/* Direct array access with offset calculation */
ngx_str_t *status_line;

if (status >= 200 && status < 300) {
    status_line = &ngx_http_status_lines[status - 200];
} else if (status >= 300 && status < 400) {
    status_line = &ngx_http_status_lines[status - 300 + NGX_HTTP_OFF_3XX];
}
/* ... additional offset logic ... */
```

**After (recommended):**
```c
const ngx_str_t *reason;

reason = ngx_http_status_reason(status);
if (reason == NULL) {
    /* Handle unknown status code */
    ngx_log_error(NGX_LOG_WARN, r->connection->log, 0,
                  "unknown status code: %ui", status);
}
```

### Step 5: Verify Compilation

Rebuild your module and verify no warnings related to deprecated patterns:

```bash
./auto/configure --add-module=/path/to/your/module
make
```

### Step 6: Test Module Functionality

Run your module's test suite ensuring:

- All status code assignments function correctly
- Error responses return expected status codes
- Reason phrases appear correctly in response headers
- Performance meets requirements (<2% latency overhead)

---

## Before/After Code Patterns

### Pattern 1: Simple Status Assignment

The most common pattern is setting a successful response status.

**Before (deprecated):**
```c
static ngx_int_t
ngx_http_mymodule_handler(ngx_http_request_t *r)
{
    /* ... request processing ... */

    r->headers_out.status = NGX_HTTP_OK;
    r->headers_out.content_length_n = content_length;

    rc = ngx_http_send_header(r);
    /* ... */
}
```

**After (recommended):**
```c
static ngx_int_t
ngx_http_mymodule_handler(ngx_http_request_t *r)
{
    /* ... request processing ... */

    ngx_http_status_set(r, NGX_HTTP_OK);
    r->headers_out.content_length_n = content_length;

    rc = ngx_http_send_header(r);
    /* ... */
}
```

### Pattern 2: Status Assignment with Error Handling

For status codes that may be dynamically determined, add validation.

**Before (deprecated):**
```c
static ngx_int_t
ngx_http_mymodule_error_handler(ngx_http_request_t *r, ngx_uint_t err)
{
    switch (err) {
    case NGX_ENOENT:
        r->headers_out.status = NGX_HTTP_NOT_FOUND;
        break;

    case NGX_EACCES:
        r->headers_out.status = NGX_HTTP_FORBIDDEN;
        break;

    default:
        r->headers_out.status = NGX_HTTP_INTERNAL_SERVER_ERROR;
        break;
    }

    return ngx_http_special_response_handler(r, r->headers_out.status);
}
```

**After (recommended):**
```c
static ngx_int_t
ngx_http_mymodule_error_handler(ngx_http_request_t *r, ngx_uint_t err)
{
    ngx_uint_t  status;

    switch (err) {
    case NGX_ENOENT:
        status = NGX_HTTP_NOT_FOUND;
        break;

    case NGX_EACCES:
        status = NGX_HTTP_FORBIDDEN;
        break;

    default:
        status = NGX_HTTP_INTERNAL_SERVER_ERROR;
        break;
    }

    if (ngx_http_status_set(r, status) != NGX_OK) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "failed to set status code: %ui", status);
        status = NGX_HTTP_INTERNAL_SERVER_ERROR;
        ngx_http_status_set(r, status);
    }

    return ngx_http_special_response_handler(r, status);
}
```

### Pattern 3: Status Reason Phrase Lookup (Header Filter)

Modules generating custom status lines should use the registry lookup.

**Before (deprecated):**
```c
static ngx_int_t
ngx_http_mymodule_header_filter(ngx_http_request_t *r)
{
    ngx_str_t   *status_line;
    ngx_uint_t   status;

    status = r->headers_out.status;

    /* Complex offset calculation for status line lookup */
    if (status >= 200 && status < 300) {
        status_line = &ngx_http_status_lines[status - 200];
    } else if (status >= 300 && status < 400) {
        status_line = &ngx_http_status_lines[status - 300 + 8];  /* offset */
    } else if (status >= 400 && status < 500) {
        status_line = &ngx_http_status_lines[status - 400 + 15]; /* offset */
    } else if (status >= 500 && status < 600) {
        status_line = &ngx_http_status_lines[status - 500 + 36]; /* offset */
    } else {
        status_line = NULL;
    }

    /* Use status_line... */
}
```

**After (recommended):**
```c
static ngx_int_t
ngx_http_mymodule_header_filter(ngx_http_request_t *r)
{
    const ngx_str_t  *reason;
    ngx_uint_t        status;

    status = r->headers_out.status;

    /* Simple registry lookup - no offset calculation needed */
    reason = ngx_http_status_reason(status);

    if (reason == NULL) {
        ngx_log_error(NGX_LOG_WARN, r->connection->log, 0,
                      "no reason phrase for status: %ui", status);
        /* Use generic reason or handle error */
    }

    /* Use reason... */
}
```

### Pattern 4: Conditional Status Check (Unchanged)

Status code comparison patterns remain unchanged.

```c
/* These patterns require NO modification */

if (r->headers_out.status == NGX_HTTP_OK) {
    /* Handle successful response */
}

if (r->headers_out.status >= NGX_HTTP_BAD_REQUEST) {
    /* Handle error response */
}

if (r->headers_out.status == NGX_HTTP_NOT_MODIFIED) {
    /* Handle 304 response - no body */
}

switch (r->headers_out.status) {
case NGX_HTTP_OK:
case NGX_HTTP_PARTIAL_CONTENT:
    /* Allow range processing */
    break;
default:
    /* Skip range processing */
    break;
}
```

### Pattern 5: Cacheability Check

Use the new API to determine default cacheability.

**Before (manual check):**
```c
static ngx_int_t
ngx_http_mymodule_is_cacheable(ngx_http_request_t *r)
{
    switch (r->headers_out.status) {
    case NGX_HTTP_OK:
    case NGX_HTTP_NO_CONTENT:
    case NGX_HTTP_PARTIAL_CONTENT:
    case NGX_HTTP_MOVED_PERMANENTLY:
    case NGX_HTTP_NOT_MODIFIED:
    case NGX_HTTP_NOT_FOUND:
    case NGX_HTTP_NOT_IMPLEMENTED:
        return 1;
    default:
        return 0;
    }
}
```

**After (using API):**
```c
static ngx_int_t
ngx_http_mymodule_is_cacheable(ngx_http_request_t *r)
{
    return ngx_http_status_is_cacheable(r->headers_out.status);
}
```

---

## Error Handling Patterns

### Validation Failure Protocol

When `ngx_http_status_set()` encounters an invalid status code:

1. The function returns `NGX_ERROR`
2. The `r->headers_out.status` field is **not modified**
3. An error is logged at `NGX_LOG_ERR` level with request context
4. The calling module should fall back to a safe status code (typically 500)

### Basic Error Handling

```c
ngx_uint_t  status;

status = compute_response_status(r);

if (ngx_http_status_set(r, status) != NGX_OK) {
    /* Validation failed - use safe fallback */
    return NGX_HTTP_INTERNAL_SERVER_ERROR;
}
```

### Comprehensive Error Handling with Logging

```c
ngx_uint_t  status;

status = compute_response_status(r);

if (ngx_http_status_set(r, status) != NGX_OK) {
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                  "mymodule: invalid status code %ui, using 500", status);

    /* Set safe fallback status */
    if (ngx_http_status_set(r, NGX_HTTP_INTERNAL_SERVER_ERROR) != NGX_OK) {
        /* Critical failure - this should never happen for 500 */
        ngx_log_error(NGX_LOG_ALERT, r->connection->log, 0,
                      "mymodule: failed to set fallback status 500");
        return NGX_ERROR;
    }
}
```

### Handling Dynamic Status Codes

For modules that accept user-configured or upstream-provided status codes:

```c
static ngx_int_t
ngx_http_mymodule_set_dynamic_status(ngx_http_request_t *r,
    ngx_uint_t user_status)
{
    /* Validate before attempting to set */
    if (ngx_http_status_validate(user_status) != NGX_OK) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "mymodule: user-provided status %ui is invalid",
                      user_status);

        /* Option 1: Return error to caller */
        return NGX_ERROR;

        /* Option 2: Use default status */
        /* user_status = NGX_HTTP_OK; */
    }

    /* Safe to set - validation passed */
    ngx_http_status_set(r, user_status);

    return NGX_OK;
}
```

### Error Handling in Filter Chains

Filter modules should propagate errors appropriately:

```c
static ngx_int_t
ngx_http_mymodule_header_filter(ngx_http_request_t *r)
{
    if (some_error_condition) {
        /*
         * In filter context, we modify the existing status.
         * Log and set error status, then continue filter chain.
         */
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "mymodule filter error, changing status to 500");

        if (ngx_http_status_set(r, NGX_HTTP_INTERNAL_SERVER_ERROR) != NGX_OK) {
            /* Should never fail for standard codes */
            return NGX_ERROR;
        }
    }

    return ngx_http_next_header_filter(r);
}
```

### Debug Logging Format

The internal logging uses the format:

```
"http status set: %ui %V (valid|invalid)"
```

Example log entries:

```
2026/01/15 10:30:45 [debug] 1234#0: *5 http status set: 200 OK (valid)
2026/01/15 10:30:46 [error] 1234#0: *6 http status set: 999 (invalid)
2026/01/15 10:30:47 [debug] 1234#0: *7 http status set: 404 Not Found (valid)
```

---

## Common Status Code Examples

### 2xx Success Codes

#### 200 OK - Successful Request

```c
static ngx_int_t
ngx_http_mymodule_handler(ngx_http_request_t *r)
{
    ngx_buf_t    *b;
    ngx_chain_t   out;

    /* Prepare response content */
    b = ngx_calloc_buf(r->pool);
    if (b == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    /* Set success status */
    ngx_http_status_set(r, NGX_HTTP_OK);
    r->headers_out.content_type_len = sizeof("text/plain") - 1;
    ngx_str_set(&r->headers_out.content_type, "text/plain");
    r->headers_out.content_length_n = response_len;

    /* Send response */
    ngx_http_send_header(r);

    out.buf = b;
    out.next = NULL;

    return ngx_http_output_filter(r, &out);
}
```

#### 201 Created - Resource Creation

```c
static ngx_int_t
ngx_http_mymodule_create_resource(ngx_http_request_t *r)
{
    ngx_table_elt_t  *location;

    /* Create the resource */
    if (create_resource(r) != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    /* Set 201 Created status */
    ngx_http_status_set(r, NGX_HTTP_CREATED);

    /* Add Location header pointing to new resource */
    location = ngx_list_push(&r->headers_out.headers);
    if (location == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    location->hash = 1;
    ngx_str_set(&location->key, "Location");
    location->value = new_resource_uri;

    return ngx_http_send_header(r);
}
```

#### 204 No Content

```c
static ngx_int_t
ngx_http_mymodule_delete_handler(ngx_http_request_t *r)
{
    /* Perform deletion */
    if (delete_resource(r) != NGX_OK) {
        return NGX_HTTP_NOT_FOUND;
    }

    /* 204 No Content - successful deletion with no response body */
    ngx_http_status_set(r, NGX_HTTP_NO_CONTENT);
    r->headers_out.content_length_n = 0;
    r->header_only = 1;

    return ngx_http_send_header(r);
}
```

### 3xx Redirection Codes

#### 301 Moved Permanently

```c
static ngx_int_t
ngx_http_mymodule_redirect_permanent(ngx_http_request_t *r, ngx_str_t *uri)
{
    ngx_table_elt_t  *location;

    location = ngx_list_push(&r->headers_out.headers);
    if (location == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    location->hash = 1;
    ngx_str_set(&location->key, "Location");
    location->value = *uri;

    ngx_http_status_set(r, NGX_HTTP_MOVED_PERMANENTLY);

    return NGX_HTTP_MOVED_PERMANENTLY;
}
```

#### 302 Found (Temporary Redirect)

```c
static ngx_int_t
ngx_http_mymodule_redirect_temporary(ngx_http_request_t *r, ngx_str_t *uri)
{
    r->headers_out.location = ngx_list_push(&r->headers_out.headers);
    if (r->headers_out.location == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    r->headers_out.location->hash = 1;
    ngx_str_set(&r->headers_out.location->key, "Location");
    r->headers_out.location->value = *uri;

    ngx_http_status_set(r, NGX_HTTP_MOVED_TEMPORARILY);

    return NGX_HTTP_MOVED_TEMPORARILY;
}
```

#### 304 Not Modified

```c
static ngx_int_t
ngx_http_mymodule_not_modified_filter(ngx_http_request_t *r)
{
    time_t  ims;

    if (r->headers_in.if_modified_since == NULL) {
        return ngx_http_next_header_filter(r);
    }

    ims = ngx_parse_http_time(r->headers_in.if_modified_since->value.data,
                              r->headers_in.if_modified_since->value.len);

    if (ims != NGX_ERROR && ims >= r->headers_out.last_modified_time) {
        /* Resource not modified - send 304 */
        ngx_http_status_set(r, NGX_HTTP_NOT_MODIFIED);
        r->header_only = 1;
    }

    return ngx_http_next_header_filter(r);
}
```

### 4xx Client Error Codes

#### 400 Bad Request

```c
static ngx_int_t
ngx_http_mymodule_validate_request(ngx_http_request_t *r)
{
    if (r->headers_in.content_type == NULL) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "mymodule: missing Content-Type header");
        return NGX_HTTP_BAD_REQUEST;
    }

    if (validate_content_type(r) != NGX_OK) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "mymodule: invalid Content-Type");

        ngx_http_status_set(r, NGX_HTTP_BAD_REQUEST);
        return NGX_HTTP_BAD_REQUEST;
    }

    return NGX_OK;
}
```

#### 401 Unauthorized

```c
static ngx_int_t
ngx_http_mymodule_auth_handler(ngx_http_request_t *r)
{
    ngx_table_elt_t  *www_auth;

    if (authenticate_user(r) != NGX_OK) {
        /* Authentication failed - require credentials */
        www_auth = ngx_list_push(&r->headers_out.headers);
        if (www_auth == NULL) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }

        www_auth->hash = 1;
        ngx_str_set(&www_auth->key, "WWW-Authenticate");
        ngx_str_set(&www_auth->value, "Basic realm=\"Protected Area\"");

        ngx_http_status_set(r, NGX_HTTP_UNAUTHORIZED);

        return NGX_HTTP_UNAUTHORIZED;
    }

    return NGX_DECLINED;
}
```

#### 403 Forbidden

```c
static ngx_int_t
ngx_http_mymodule_access_handler(ngx_http_request_t *r)
{
    if (check_access_rules(r) != NGX_OK) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "mymodule: access denied for %V", &r->uri);

        ngx_http_status_set(r, NGX_HTTP_FORBIDDEN);

        return NGX_HTTP_FORBIDDEN;
    }

    return NGX_DECLINED;
}
```

#### 404 Not Found

```c
static ngx_int_t
ngx_http_mymodule_handler(ngx_http_request_t *r)
{
    ngx_int_t  rc;

    rc = lookup_resource(r);

    if (rc == NGX_DECLINED) {
        ngx_log_error(NGX_LOG_INFO, r->connection->log, 0,
                      "mymodule: resource not found: %V", &r->uri);

        ngx_http_status_set(r, NGX_HTTP_NOT_FOUND);

        return NGX_HTTP_NOT_FOUND;
    }

    return rc;
}
```

#### 405 Method Not Allowed

```c
static ngx_int_t
ngx_http_mymodule_handler(ngx_http_request_t *r)
{
    ngx_table_elt_t  *allow;

    if (!(r->method & (NGX_HTTP_GET | NGX_HTTP_HEAD | NGX_HTTP_POST))) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "mymodule: method %ui not allowed", r->method);

        /* Add Allow header listing permitted methods */
        allow = ngx_list_push(&r->headers_out.headers);
        if (allow != NULL) {
            allow->hash = 1;
            ngx_str_set(&allow->key, "Allow");
            ngx_str_set(&allow->value, "GET, HEAD, POST");
        }

        ngx_http_status_set(r, NGX_HTTP_NOT_ALLOWED);

        return NGX_HTTP_NOT_ALLOWED;
    }

    /* Continue processing */
    return NGX_DECLINED;
}
```

#### 429 Too Many Requests

```c
static ngx_int_t
ngx_http_mymodule_rate_limit(ngx_http_request_t *r)
{
    ngx_table_elt_t  *retry_after;

    if (check_rate_limit(r) != NGX_OK) {
        ngx_log_error(NGX_LOG_WARN, r->connection->log, 0,
                      "mymodule: rate limit exceeded for client %V",
                      &r->connection->addr_text);

        /* Add Retry-After header */
        retry_after = ngx_list_push(&r->headers_out.headers);
        if (retry_after != NULL) {
            retry_after->hash = 1;
            ngx_str_set(&retry_after->key, "Retry-After");
            ngx_str_set(&retry_after->value, "60");
        }

        ngx_http_status_set(r, NGX_HTTP_TOO_MANY_REQUESTS);

        return NGX_HTTP_TOO_MANY_REQUESTS;
    }

    return NGX_DECLINED;
}
```

### 5xx Server Error Codes

#### 500 Internal Server Error

```c
static ngx_int_t
ngx_http_mymodule_handler(ngx_http_request_t *r)
{
    ngx_int_t  rc;

    rc = process_request(r);

    if (rc == NGX_ERROR) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "mymodule: internal processing error");

        ngx_http_status_set(r, NGX_HTTP_INTERNAL_SERVER_ERROR);

        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    return rc;
}
```

#### 502 Bad Gateway

```c
static ngx_int_t
ngx_http_mymodule_upstream_error(ngx_http_request_t *r, ngx_int_t error_code)
{
    if (error_code == NGX_HTTP_UPSTREAM_INVALID_HEADER) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "mymodule: upstream sent invalid response");

        ngx_http_status_set(r, NGX_HTTP_BAD_GATEWAY);

        return NGX_HTTP_BAD_GATEWAY;
    }

    return NGX_HTTP_INTERNAL_SERVER_ERROR;
}
```

#### 503 Service Unavailable

```c
static ngx_int_t
ngx_http_mymodule_maintenance_check(ngx_http_request_t *r)
{
    ngx_table_elt_t  *retry_after;

    if (is_maintenance_mode()) {
        ngx_log_error(NGX_LOG_INFO, r->connection->log, 0,
                      "mymodule: service in maintenance mode");

        /* Add Retry-After header */
        retry_after = ngx_list_push(&r->headers_out.headers);
        if (retry_after != NULL) {
            retry_after->hash = 1;
            ngx_str_set(&retry_after->key, "Retry-After");
            ngx_str_set(&retry_after->value, "300");
        }

        ngx_http_status_set(r, NGX_HTTP_SERVICE_UNAVAILABLE);

        return NGX_HTTP_SERVICE_UNAVAILABLE;
    }

    return NGX_DECLINED;
}
```

---

## Backward Compatibility

### Preserved Functionality

The following legacy patterns continue to work without modification:

#### Existing NGX_HTTP_* Macros

All status code macros remain defined and functional:

```c
/* These constants are preserved and fully functional */
#define NGX_HTTP_OK                        200
#define NGX_HTTP_CREATED                   201
#define NGX_HTTP_NO_CONTENT                204
#define NGX_HTTP_MOVED_PERMANENTLY         301
#define NGX_HTTP_MOVED_TEMPORARILY         302
#define NGX_HTTP_NOT_MODIFIED              304
#define NGX_HTTP_BAD_REQUEST               400
#define NGX_HTTP_UNAUTHORIZED              401
#define NGX_HTTP_FORBIDDEN                 403
#define NGX_HTTP_NOT_FOUND                 404
#define NGX_HTTP_INTERNAL_SERVER_ERROR     500
/* ... and all others */
```

#### Direct Status Assignment

The deprecated pattern still compiles and works:

```c
/* DEPRECATED but still functional */
r->headers_out.status = NGX_HTTP_OK;
r->headers_out.status = 404;
r->headers_out.status = custom_status;
```

**Note:** Direct assignment bypasses validation. Use `ngx_http_status_set()`
for new development to benefit from RFC 9110 compliance checking.

#### Status Comparison Patterns

All comparison patterns remain unchanged:

```c
/* These patterns require NO changes */
if (r->headers_out.status == NGX_HTTP_OK) { ... }
if (r->headers_out.status != NGX_HTTP_NOT_FOUND) { ... }
if (r->headers_out.status >= NGX_HTTP_BAD_REQUEST) { ... }
if (r->headers_out.status >= 400 && r->headers_out.status < 500) { ... }

switch (r->headers_out.status) {
case NGX_HTTP_OK:
    break;
case NGX_HTTP_NOT_FOUND:
    break;
default:
    break;
}
```

### No Forced Migration Timeline

- **Immediate:** Existing modules continue to work without changes
- **Recommended:** New development should use the unified API
- **Future:** Direct assignment may generate deprecation warnings in later
  releases but will not be removed

### ABI Compatibility

- Third-party modules compiled against older NGINX headers remain compatible
- No recompilation required for existing binary modules
- `NGX_MODULE_V1` interface unchanged

---

## API Reference Summary

### ngx_http_status_set

```c
ngx_int_t ngx_http_status_set(ngx_http_request_t *r, ngx_uint_t status);
```

**Purpose:** Set the HTTP response status code with validation.

**Parameters:**

| Parameter | Type | Description |
|-----------|------|-------------|
| `r` | `ngx_http_request_t *` | Request object |
| `status` | `ngx_uint_t` | HTTP status code (100-599) |

**Returns:**

| Value | Description |
|-------|-------------|
| `NGX_OK` | Status code set successfully |
| `NGX_ERROR` | Invalid status code (validation failed) |

**Behavior:**
- Sets `r->headers_out.status` to the provided value
- In strict mode: validates range 100-599 per RFC 9110
- In standard mode: accepts any value but logs warnings for suspicious codes
- Logs validation failures at `NGX_LOG_ERR`

**Example:**
```c
if (ngx_http_status_set(r, NGX_HTTP_OK) != NGX_OK) {
    return NGX_HTTP_INTERNAL_SERVER_ERROR;
}
```

### ngx_http_status_validate

```c
ngx_int_t ngx_http_status_validate(ngx_uint_t status);
```

**Purpose:** Validate a status code without setting it.

**Parameters:**

| Parameter | Type | Description |
|-----------|------|-------------|
| `status` | `ngx_uint_t` | HTTP status code to validate |

**Returns:**

| Value | Description |
|-------|-------------|
| `NGX_OK` | Status code is valid |
| `NGX_ERROR` | Status code is invalid |

**Behavior:**
- Checks if status code is within valid range (100-599)
- Does not modify any request state
- Useful for pre-validation of user-provided or upstream status codes

**Example:**
```c
if (ngx_http_status_validate(user_provided_status) != NGX_OK) {
    ngx_log_error(NGX_LOG_ERR, log, 0, "invalid status: %ui",
                  user_provided_status);
    return NGX_ERROR;
}
```

### ngx_http_status_reason

```c
const ngx_str_t *ngx_http_status_reason(ngx_uint_t status);
```

**Purpose:** Get the reason phrase for an HTTP status code.

**Parameters:**

| Parameter | Type | Description |
|-----------|------|-------------|
| `status` | `ngx_uint_t` | HTTP status code |

**Returns:**

| Value | Description |
|-------|-------------|
| `ngx_str_t *` | Pointer to reason phrase string |
| `NULL` | Unknown status code |

**Behavior:**
- Returns pointer to statically allocated reason phrase
- Thread-safe (read-only registry access)
- O(1) lookup performance via direct array indexing

**Example:**
```c
const ngx_str_t *reason = ngx_http_status_reason(404);
if (reason != NULL) {
    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, log, 0,
                   "status %ui: %V", 404, reason);
}
/* Output: "status 404: Not Found" */
```

### ngx_http_status_is_cacheable

```c
ngx_int_t ngx_http_status_is_cacheable(ngx_uint_t status);
```

**Purpose:** Determine if a status code indicates a cacheable response.

**Parameters:**

| Parameter | Type | Description |
|-----------|------|-------------|
| `status` | `ngx_uint_t` | HTTP status code |

**Returns:**

| Value | Description |
|-------|-------------|
| `1` (true) | Response is cacheable by default |
| `0` (false) | Response is not cacheable by default |

**Behavior:**
- Returns cacheability per RFC 9111 defaults
- Cacheable by default: 200, 203, 204, 206, 300, 301, 308, 404, 405, 410, 414, 501
- Does not consider Cache-Control headers (only status code)

**Example:**
```c
if (ngx_http_status_is_cacheable(r->headers_out.status)) {
    /* Consider caching this response */
}
```

---

## Special Considerations

### Upstream/Proxy Module Behavior

Modules that proxy requests to upstream servers should **preserve pass-through
behavior**. Status codes from backends are passed through without validation:

```c
static ngx_int_t
ngx_http_myproxy_process_header(ngx_http_request_t *r)
{
    ngx_http_upstream_t  *u;

    u = r->upstream;

    /*
     * Upstream status codes pass through directly.
     * The API detects r->upstream presence and bypasses strict validation.
     */
    ngx_http_status_set(r, u->headers_in.status_n);

    return NGX_OK;
}
```

**Important:** When `r->upstream` is set, the status API operates in
permissive mode to allow any status code from the backend server.

### Build Configuration

#### Standard Build (Default)

```bash
./auto/configure
make
```

Standard mode provides:
- Range validation (100-599)
- Debug-level logging for suspicious codes
- Full backward compatibility

#### Strict RFC 9110 Mode

```bash
./auto/configure --with-http_status_validation
make
```

Strict mode adds:
- Enforced RFC 9110 compliance
- `NGX_LOG_WARN` (production) or `NGX_LOG_ERR` (debug build) for violations
- Informational code (1xx) validation before final response

### Performance Characteristics

The new API is designed to meet the **<2% latency overhead** requirement:

| Operation | Complexity | Overhead |
|-----------|------------|----------|
| `ngx_http_status_set()` | O(1) | <10 CPU cycles vs direct assignment |
| `ngx_http_status_validate()` | O(1) | ~5 CPU cycles |
| `ngx_http_status_reason()` | O(1) | Direct array lookup |
| `ngx_http_status_is_cacheable()` | O(1) | Flag check |

**Memory footprint:** The status code registry consumes <1KB per worker
process (stored in read-only data section).

### Thread Safety

The status code registry is **immutable after initialization**:

- Registry populated during configuration parsing phase
- Workers fork with read-only copy of registry data
- No locks or synchronization required for lookups
- Thread-safe by design (copy-on-write semantics)

### NGINX-Specific Extension Codes

NGINX defines several internal status codes outside the standard HTTP range:

| Code | Constant | Purpose |
|------|----------|---------|
| 444 | `NGX_HTTP_CLOSE` | Close connection without response |
| 494 | `NGX_HTTP_REQUEST_HEADER_TOO_LARGE` | Request header exceeded limit |
| 495 | `NGX_HTTPS_CERT_ERROR` | SSL certificate error |
| 496 | `NGX_HTTPS_NO_CERT` | No SSL certificate provided |
| 497 | `NGX_HTTP_TO_HTTPS` | HTTP request to HTTPS port |
| 499 | `NGX_HTTP_CLIENT_CLOSED_REQUEST` | Client closed connection |

These codes are **accepted by the API** in both standard and strict modes
as they are valid NGINX internal codes, even though they are not standard
HTTP status codes.

---

## Changelog Reference

For version history and release notes related to the status code API
refactoring, refer to:

- **NGINX Changes:** `docs/xml/nginx/changes.xml`
- **Release Notes:** Check the official NGINX release announcements

The status code registry feature is documented in the changes file with:

```xml
<change type="feature">
<para lang="en">
the HTTP status code registry with unified API for status code
validation, reason phrase lookup, and RFC 9110 compliance checking.
</para>
</change>
```

---

## Quick Reference Card

### Migration Checklist

- [ ] Search for `r->headers_out.status =` patterns
- [ ] Replace with `ngx_http_status_set(r, status)`
- [ ] Add error handling for dynamic status codes
- [ ] Replace `ngx_http_status_lines[]` with `ngx_http_status_reason()`
- [ ] Test with both standard and strict build modes
- [ ] Verify performance meets requirements

### API Quick Reference

| Function | Purpose | Returns |
|----------|---------|---------|
| `ngx_http_status_set(r, status)` | Set status with validation | `NGX_OK` / `NGX_ERROR` |
| `ngx_http_status_validate(status)` | Validate without setting | `NGX_OK` / `NGX_ERROR` |
| `ngx_http_status_reason(status)` | Get reason phrase | `ngx_str_t *` / `NULL` |
| `ngx_http_status_is_cacheable(status)` | Check cacheability | `1` / `0` |

### Common Patterns

```c
/* Set status (simple) */
ngx_http_status_set(r, NGX_HTTP_OK);

/* Set status (with error handling) */
if (ngx_http_status_set(r, status) != NGX_OK) {
    return NGX_HTTP_INTERNAL_SERVER_ERROR;
}

/* Get reason phrase */
const ngx_str_t *reason = ngx_http_status_reason(status);

/* Check cacheability */
if (ngx_http_status_is_cacheable(status)) { ... }
```

---

*Copyright (C) Nginx, Inc.*
