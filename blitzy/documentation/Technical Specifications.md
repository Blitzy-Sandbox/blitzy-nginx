# Technical Specification

# 0. Agent Action Plan

## 0.1 Intent Clarification

Based on the prompt, the Blitzy platform understands that the refactoring objective is to transform NGINX's HTTP status code handling from a scattered, constant-based implementation to a centralized registry-based architecture with a unified API layer, enabling RFC 9110 compliance validation while maintaining complete backward compatibility with existing NGINX deployments.

### 0.1.1 Core Refactoring Objective

**Refactoring Type:** Code structure refactoring with design pattern modernization

**Target Repository:** Same repository (in-place refactoring of NGINX core)

**Primary Goals:**

| Goal | Description | Success Criteria |
|------|-------------|------------------|
| Centralize Status Code Definitions | Replace scattered `#define` macros with a structured registry containing metadata | Single source of truth for all HTTP status codes |
| Implement Unified API | Create `ngx_http_status_set()` and related functions as the canonical interface | All status code operations flow through new API |
| Enable RFC 9110 Compliance | Add validation layer that can verify status codes against HTTP Semantics specification | Optional strict mode validates range (100-599) |
| Preserve Backward Compatibility | Existing constants and direct assignment patterns remain functional | Zero functional regression in existing behavior |

**Implicit Requirements Surfaced:**

- Existing `NGX_HTTP_*` macro constants must continue to compile and resolve to their numeric values
- Direct assignment to `r->headers_out.status` cannot be removed immediately (gradual deprecation)
- Performance overhead must remain below 2% latency impact on hot paths
- Thread safety achieved through registry immutability after worker initialization
- No modifications to event loop, configuration parser, or memory allocator subsystems

### 0.1.2 Special Instructions and Constraints

**Critical Directives from User:**

| Directive | Implementation Requirement |
|-----------|---------------------------|
| Maintain all public interfaces | `NGX_MODULE_V1` interface remains unchanged; no filter chain API changes |
| Preserve test coverage | nginx-tests suite must pass for all status code-related scenarios |
| Performance threshold | <2% latency increase measured via wrk benchmarks |
| Graceful upgrade | New binary must work with existing nginx.conf without modifications |
| Memory footprint | Registry must consume <1KB per worker process |

**Migration Requirements:**

- Module conversion sequence: `ngx_http_core_module.c` → `ngx_http_request.c` → `ngx_http_static_module.c` → proxy/fastcgi/uwsgi modules → remaining modules
- Validation gate required after each module group conversion
- Rollback to previous module version if any nginx-tests failure detected

**Preserved Behavior (Do Not Modify):**

- `error_page` directive functionality
- `return` directive status code handling in `ngx_http_rewrite_module`
- `proxy_intercept_errors` behavior for upstream status pass-through
- Access log format (status code field remains numeric)

**User Example (Preserved Exactly):**
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

### 0.1.3 Technical Interpretation

This refactoring translates to the following technical transformation strategy:

**Architecture Transformation:**

```
Current State:                          Target State:
┌─────────────────────┐                 ┌─────────────────────┐
│ Scattered #defines  │                 │ Centralized Registry│
│ in ngx_http_request.h│       →        │ ngx_http_status_def_t│
│ (lines 79-145)      │                 │ static array        │
└─────────────────────┘                 └─────────────────────┘
          │                                       │
          ▼                                       ▼
┌─────────────────────┐                 ┌─────────────────────┐
│ Direct assignment   │                 │ Unified API Layer   │
│ r->headers_out.status│       →        │ ngx_http_status_set()│
│ = NGX_HTTP_OK       │                 │ ngx_http_status_reason()│
└─────────────────────┘                 └─────────────────────┘
          │                                       │
          ▼                                       ▼
┌─────────────────────┐                 ┌─────────────────────┐
│ Static string array │                 │ Registry-based      │
│ ngx_http_status_lines│       →        │ reason phrase lookup│
│ in header filter    │                 │ via API             │
└─────────────────────┘                 └─────────────────────┘
```

**Transformation Rules:**

| Source Pattern | Target Pattern | Scope |
|----------------|----------------|-------|
| `r->headers_out.status = NGX_HTTP_*` | `ngx_http_status_set(r, code)` | All HTTP modules (28 files) |
| `r->headers_out.status == NGX_HTTP_*` | No change (comparison preserved) | All conditional checks |
| `ngx_http_status_lines[index]` | `ngx_http_status_reason(code)` | Header filter, v2/v3 filters |
| `#define NGX_HTTP_OK 200` | Preserved + registry entry | Backward compatibility |

**API Contract Specifications:**

- `ngx_http_status_set()` validates status code range (100-599 per RFC 9110)
- Validation failures logged at `NGX_LOG_ERR` level with request context
- Performance target: <10 CPU cycles overhead versus direct assignment
- Registry lookup via computed array offset achieves O(1) complexity


## 0.2 Source Analysis

### 0.2.1 Comprehensive Source File Discovery

**Status Code Definition Locations:**

The following files contain HTTP status code macro definitions that form the foundation of the current implementation:

| File Path | Lines | Content Description |
|-----------|-------|---------------------|
| `src/http/ngx_http_request.h` | 79-145 | Primary status code `#define` macros (2xx through 5xx) |

**Discovered Status Code Definitions (lines 79-145):**

```
2xx Success:       NGX_HTTP_OK (200), NGX_HTTP_CREATED (201), 
                   NGX_HTTP_ACCEPTED (202), NGX_HTTP_NO_CONTENT (204),
                   NGX_HTTP_PARTIAL_CONTENT (206)

3xx Redirection:   NGX_HTTP_SPECIAL_RESPONSE (300), NGX_HTTP_MOVED_PERMANENTLY (301),
                   NGX_HTTP_MOVED_TEMPORARILY (302), NGX_HTTP_SEE_OTHER (303),
                   NGX_HTTP_NOT_MODIFIED (304), NGX_HTTP_TEMPORARY_REDIRECT (307),
                   NGX_HTTP_PERMANENT_REDIRECT (308)

4xx Client Error:  NGX_HTTP_BAD_REQUEST (400), NGX_HTTP_UNAUTHORIZED (401),
                   NGX_HTTP_FORBIDDEN (403), NGX_HTTP_NOT_FOUND (404),
                   NGX_HTTP_NOT_ALLOWED (405), NGX_HTTP_REQUEST_TIME_OUT (408),
                   NGX_HTTP_CONFLICT (409), NGX_HTTP_LENGTH_REQUIRED (411),
                   NGX_HTTP_PRECONDITION_FAILED (412), NGX_HTTP_REQUEST_ENTITY_TOO_LARGE (413),
                   NGX_HTTP_REQUEST_URI_TOO_LARGE (414), NGX_HTTP_UNSUPPORTED_MEDIA_TYPE (415),
                   NGX_HTTP_RANGE_NOT_SATISFIABLE (416), NGX_HTTP_MISDIRECTED_REQUEST (421),
                   NGX_HTTP_TOO_MANY_REQUESTS (429), NGX_HTTP_CLOSE (444),
                   NGX_HTTP_REQUEST_HEADER_TOO_LARGE (494), NGX_HTTP_TO_HTTPS (497),
                   NGX_HTTP_CLIENT_CLOSED_REQUEST (499)

5xx Server Error:  NGX_HTTP_INTERNAL_SERVER_ERROR (500), NGX_HTTP_NOT_IMPLEMENTED (501),
                   NGX_HTTP_BAD_GATEWAY (502), NGX_HTTP_SERVICE_UNAVAILABLE (503),
                   NGX_HTTP_GATEWAY_TIME_OUT (504), NGX_HTTP_VERSION_NOT_SUPPORTED (505),
                   NGX_HTTP_INSUFFICIENT_STORAGE (507)
```

### 0.2.2 Status Code Assignment Locations

**Files with Direct `r->headers_out.status` Assignments (76 total occurrences):**

| File Path | Occurrences | Primary Use Case |
|-----------|-------------|------------------|
| `src/http/ngx_http_header_filter_module.c` | 10 | Status line serialization, switch statement for status ranges |
| `src/http/v3/ngx_http_v3_filter_module.c` | 9 | HTTP/3 response framing and QPACK encoding |
| `src/http/modules/ngx_http_slice_filter_module.c` | 6 | Range request slice handling |
| `src/http/v2/ngx_http_v2_filter_module.c` | 4 | HTTP/2 HEADERS frame construction |
| `src/http/ngx_http_request.c` | 4 | Request lifecycle status handling |
| `src/http/modules/ngx_http_range_filter_module.c` | 4 | Byte range response handling |
| `src/http/modules/ngx_http_chunked_filter_module.c` | 4 | Chunked transfer encoding |
| `src/http/ngx_http_variables.c` | 3 | Variable extraction for logging |
| `src/http/ngx_http_core_module.c` | 3 | Core error page generation |
| `src/http/modules/ngx_http_not_modified_filter_module.c` | 3 | 304 Not Modified handling |
| `src/http/modules/ngx_http_gzip_filter_module.c` | 3 | Compression filter conditions |
| `src/http/ngx_http_upstream.c` | 2 | Upstream response status pass-through |
| `src/http/modules/ngx_http_log_module.c` | 2 | Access log status variable |
| `src/http/modules/ngx_http_image_filter_module.c` | 2 | Image transformation status |
| `src/http/modules/ngx_http_headers_filter_module.c` | 2 | Header manipulation |
| `src/http/modules/ngx_http_charset_filter_module.c` | 2 | Charset conversion conditions |
| `src/http/modules/ngx_http_auth_request_module.c` | 2 | Subrequest authentication |
| `src/http/ngx_http_special_response.c` | 1 | Error page generation |
| `src/http/modules/ngx_http_xslt_filter_module.c` | 1 | XSLT transformation |
| `src/http/modules/ngx_http_stub_status_module.c` | 1 | Stub status response |
| `src/http/modules/ngx_http_static_module.c` | 1 | Static file serving (200 OK) |
| `src/http/modules/ngx_http_ssi_filter_module.c` | 1 | SSI processing |
| `src/http/modules/ngx_http_mp4_module.c` | 1 | MP4 streaming |
| `src/http/modules/ngx_http_gzip_static_module.c` | 1 | Pre-compressed file serving |
| `src/http/modules/ngx_http_flv_module.c` | 1 | FLV streaming |
| `src/http/modules/ngx_http_dav_module.c` | 1 | WebDAV operations |
| `src/http/modules/ngx_http_autoindex_module.c` | 1 | Directory listing (200 OK) |
| `src/http/modules/ngx_http_addition_filter_module.c` | 1 | Addition filter conditions |

### 0.2.3 Current Structure Mapping

```
Current Source Tree Structure:

src/http/
├── ngx_http.h                           # Public API declarations (ngx_http_status_t struct)
├── ngx_http_request.h                   # Status code #define macros (lines 79-145)
├── ngx_http_request.c                   # Request lifecycle, headers_out.status assignments
├── ngx_http_core_module.c               # Core error handling, status assignments
├── ngx_http_header_filter_module.c      # ngx_http_status_lines[] array, serialization
├── ngx_http_special_response.c          # Error page HTML generation
├── ngx_http_upstream.c                  # Upstream status pass-through
├── ngx_http_variables.c                 # $status variable extraction
│
├── modules/
│   ├── ngx_http_static_module.c         # Static file 200/404/403 status
│   ├── ngx_http_proxy_module.c          # Proxy pass-through
│   ├── ngx_http_fastcgi_module.c        # FastCGI backend status
│   ├── ngx_http_rewrite_module.c        # return directive status codes
│   ├── ngx_http_access_module.c         # 403 Forbidden
│   ├── ngx_http_auth_basic_module.c     # 401 Unauthorized
│   ├── ngx_http_limit_req_module.c      # 429 Too Many Requests
│   ├── ngx_http_limit_conn_module.c     # 503 Service Unavailable
│   └── [22 additional modules]          # Various status code usage
│
├── v2/
│   ├── ngx_http_v2_filter_module.c      # HTTP/2 HEADERS frame status encoding
│   └── [6 additional files]
│
└── v3/
    ├── ngx_http_v3_filter_module.c      # HTTP/3 QPACK status encoding
    └── [12 additional files]
```

### 0.2.4 Data Structure Analysis

**Current `ngx_http_headers_out_t` Structure (from `src/http/ngx_http_request.h`, line 259-294):**

```c
typedef struct {
    ngx_list_t                        headers;
    ngx_list_t                        trailers;

    ngx_uint_t                        status;        // Line 263 - Simple integer
    ngx_str_t                         status_line;   // Line 264 - Optional custom line
    // ... additional headers fields
} ngx_http_headers_out_t;
```

**Existing `ngx_http_status_t` Structure (from `src/http/ngx_http.h`, line 71-77):**

```c
typedef struct {
    ngx_uint_t           http_version;
    ngx_uint_t           code;
    ngx_uint_t           count;
    u_char              *start;
    u_char              *end;
} ngx_http_status_t;
```

This structure is used for parsing upstream response status lines, separate from the response status handling that will be refactored.

### 0.2.5 Status Line Serialization (Header Filter)

**Location:** `src/http/ngx_http_header_filter_module.c` (lines 58-136)

The `ngx_http_status_lines[]` static array maps status codes to their string representations:

```c
static ngx_str_t ngx_http_status_lines[] = {
    ngx_string("200 OK"),
    ngx_string("201 Created"),
    // ... continues for all status codes
    ngx_string("500 Internal Server Error"),
    // ...
};
```

This array will be superseded by the centralized registry's reason phrase lookup capability.


## 0.3 Target Design

### 0.3.1 Refactored Structure Planning

**Target Architecture:**

```
Target Source Tree Structure:

src/http/
├── ngx_http.h                           # UPDATE: Add API function declarations
├── ngx_http_request.h                   # UPDATE: Preserve macros, add flag definitions
├── ngx_http_request.c                   # UPDATE: Add registry + API implementations
├── ngx_http_core_module.c               # UPDATE: Migrate to ngx_http_status_set()
├── ngx_http_header_filter_module.c      # UPDATE: Use ngx_http_status_reason()
├── ngx_http_special_response.c          # UPDATE: Integrate with status API
├── ngx_http_upstream.c                  # UPDATE: Conditional validation for proxied responses
├── ngx_http_variables.c                 # REFERENCE: No changes (read-only access)
│
├── modules/
│   ├── ngx_http_static_module.c         # UPDATE: Migrate status assignments
│   ├── ngx_http_proxy_module.c          # UPDATE: Preserve upstream pass-through
│   ├── ngx_http_fastcgi_module.c        # UPDATE: Preserve upstream pass-through
│   ├── ngx_http_uwsgi_module.c          # UPDATE: Preserve upstream pass-through
│   ├── ngx_http_scgi_module.c           # UPDATE: Preserve upstream pass-through
│   ├── ngx_http_grpc_module.c           # UPDATE: Preserve upstream pass-through
│   ├── ngx_http_memcached_module.c      # UPDATE: Preserve upstream pass-through
│   ├── ngx_http_rewrite_module.c        # UPDATE: Migrate return directive handling
│   ├── ngx_http_index_module.c          # UPDATE: Migrate status assignments
│   ├── ngx_http_autoindex_module.c      # UPDATE: Migrate 200 OK assignment
│   ├── ngx_http_access_module.c         # UPDATE: Migrate 403 assignment
│   ├── ngx_http_auth_basic_module.c     # UPDATE: Migrate 401 assignment
│   ├── ngx_http_auth_request_module.c   # UPDATE: Migrate authentication status
│   ├── ngx_http_limit_req_module.c      # UPDATE: Migrate 429 assignment
│   ├── ngx_http_limit_conn_module.c     # UPDATE: Migrate 503 assignment
│   ├── ngx_http_dav_module.c            # UPDATE: Migrate WebDAV status codes
│   ├── ngx_http_flv_module.c            # UPDATE: Migrate streaming status
│   ├── ngx_http_mp4_module.c            # UPDATE: Migrate streaming status
│   ├── ngx_http_gzip_static_module.c    # UPDATE: Migrate pre-compressed status
│   ├── ngx_http_stub_status_module.c    # UPDATE: Migrate stub status 200
│   ├── ngx_http_empty_gif_module.c      # UPDATE: Migrate 200 OK
│   ├── ngx_http_random_index_module.c   # UPDATE: Migrate index status
│   │
│   │ # Filter modules (conditional checks, minimal changes)
│   ├── ngx_http_gzip_filter_module.c        # UPDATE: Status checks preserved
│   ├── ngx_http_chunked_filter_module.c     # UPDATE: Status checks preserved
│   ├── ngx_http_range_filter_module.c       # UPDATE: Status checks preserved
│   ├── ngx_http_slice_filter_module.c       # UPDATE: Status checks preserved
│   ├── ngx_http_not_modified_filter_module.c # UPDATE: 304 handling
│   ├── ngx_http_headers_filter_module.c     # UPDATE: Header manipulation
│   ├── ngx_http_charset_filter_module.c     # UPDATE: Status checks
│   ├── ngx_http_ssi_filter_module.c         # UPDATE: SSI status handling
│   ├── ngx_http_xslt_filter_module.c        # UPDATE: XSLT status handling
│   ├── ngx_http_image_filter_module.c       # UPDATE: Image filter status
│   └── ngx_http_addition_filter_module.c    # UPDATE: Addition filter status
│
├── v2/
│   ├── ngx_http_v2_filter_module.c      # UPDATE: Use registry for HPACK encoding
│   └── [other v2 files unchanged]
│
└── v3/
    ├── ngx_http_v3_filter_module.c      # UPDATE: Use registry for QPACK encoding
    └── [other v3 files unchanged]

auto/
├── options                              # UPDATE: Add --with-http_status_validation flag
└── modules                              # UPDATE: Build system integration

docs/
├── api/
│   └── status_codes.md                  # CREATE: API reference documentation
├── migration/
│   └── status_code_api.md               # CREATE: Module developer migration guide
└── xml/nginx/
    └── changes.xml                      # UPDATE: Add changelog entry
```

### 0.3.2 Web Search Research Conducted

**RFC 9110 HTTP Semantics Compliance:**

- RFC 9110 defines status codes in Section 15 as three-digit integers
- Valid range: 100-599 (first digit determines class: 1xx-5xx)
- Status codes must include a reason phrase for human readability
- 1xx codes are informational and should not be cached
- Cacheable by default: 200, 203, 204, 206, 300, 301, 308, 404, 405, 410, 414, 501

**Registry Pattern Best Practices:**

- Centralized management provides single source of truth for status code metadata
- Static initialization at compile time ensures zero runtime allocation overhead
- Immutable registry after initialization guarantees thread safety
- Direct array indexing (O(1) lookup) optimal for status code range (100-599)

**C Static Lookup Table Patterns:**

- Const-qualified static arrays stored in read-only data section
- Compile-time initialization eliminates runtime setup costs
- Struct arrays enable metadata bundling (code, reason, flags, RFC reference)

### 0.3.3 Design Pattern Applications

**Registry Pattern Implementation:**

```c
// New structure in src/http/ngx_http_request.c
typedef struct {
    ngx_uint_t    code;          // HTTP status code (100-599)
    ngx_str_t     reason;        // Reason phrase per RFC 9110
    ngx_uint_t    flags;         // NGX_HTTP_STATUS_CACHEABLE | CLIENT_ERROR | SERVER_ERROR
    const char   *rfc_section;   // RFC 9110 section reference
} ngx_http_status_def_t;
```

**Flag Definitions:**

```c
// New constants in src/http/ngx_http_request.h
#define NGX_HTTP_STATUS_CACHEABLE       0x0001
#define NGX_HTTP_STATUS_CLIENT_ERROR    0x0002
#define NGX_HTTP_STATUS_SERVER_ERROR    0x0004
#define NGX_HTTP_STATUS_INFORMATIONAL   0x0008
```

**API Function Signatures:**

```c
// Declarations in src/http/ngx_http.h
ngx_int_t ngx_http_status_set(ngx_http_request_t *r, ngx_uint_t status);
ngx_int_t ngx_http_status_validate(ngx_uint_t status);
const ngx_str_t *ngx_http_status_reason(ngx_uint_t status);
ngx_int_t ngx_http_status_is_cacheable(ngx_uint_t status);
```

### 0.3.4 Registry Data Model

**RFC 9110 Section 15 Standard Codes Registry:**

| Class | Codes | Count | Cacheability Default |
|-------|-------|-------|---------------------|
| 1xx Informational | 100, 101 | 2 | Non-cacheable |
| 2xx Success | 200, 201, 202, 204, 206 | 5 | 200, 204, 206 cacheable |
| 3xx Redirection | 301, 302, 303, 304, 307, 308 | 6 | 301, 304, 308 cacheable |
| 4xx Client Error | 400, 401, 403, 404, 405, 408, 409, 411, 412, 413, 414, 415, 416, 421, 429 | 15 | 404, 405, 410, 414 cacheable |
| 5xx Server Error | 500, 501, 502, 503, 504, 505, 507 | 7 | 501 cacheable |

**NGINX-Specific Extension Codes:**

| Code | Constant | Usage |
|------|----------|-------|
| 444 | `NGX_HTTP_CLOSE` | Close connection without response |
| 494 | `NGX_HTTP_REQUEST_HEADER_TOO_LARGE` | Header size exceeded |
| 497 | `NGX_HTTP_TO_HTTPS` | HTTP to HTTPS redirect required |
| 499 | `NGX_HTTP_CLIENT_CLOSED_REQUEST` | Client closed connection |

### 0.3.5 Build System Integration

**Configure Flag Addition (`auto/options`):**

```shell
# Existing: HTTP_STATUS=NO (line 80)

#### Add validation flag handling

HTTP_STATUS_VALIDATION=NO
```

**Configure Option Processing:**

```shell
--with-http_status_validation)   HTTP_STATUS_VALIDATION=YES   ;;
```

This enables compile-time selection of strict RFC 9110 validation mode versus permissive mode for backward compatibility.


## 0.4 Transformation Mapping

### 0.4.1 File-by-File Transformation Plan

**File Transformation Modes:**
- **UPDATE** - Modify existing file with new API integration
- **CREATE** - Create new file (documentation only in this refactor)
- **REFERENCE** - Use as pattern reference for consistent implementation

**Core HTTP Infrastructure Files:**

| Target File | Transformation | Source File | Key Changes |
|-------------|---------------|-------------|-------------|
| `src/http/ngx_http.h` | UPDATE | `src/http/ngx_http.h` | Add API function declarations for `ngx_http_status_set()`, `ngx_http_status_validate()`, `ngx_http_status_reason()`, `ngx_http_status_is_cacheable()` |
| `src/http/ngx_http_request.h` | UPDATE | `src/http/ngx_http_request.h` | Preserve existing macros (lines 79-145), add flag definitions (`NGX_HTTP_STATUS_CACHEABLE`, etc.) |
| `src/http/ngx_http_request.c` | UPDATE | `src/http/ngx_http_request.c` | Add registry implementation (`ngx_http_status_def_t` array), implement all API functions, migrate 4 status assignments |
| `src/http/ngx_http_core_module.c` | UPDATE | `src/http/ngx_http_core_module.c` | Migrate 3 `r->headers_out.status` assignments to `ngx_http_status_set()` |
| `src/http/ngx_http_header_filter_module.c` | UPDATE | `src/http/ngx_http_header_filter_module.c` | Replace `ngx_http_status_lines[]` lookups with `ngx_http_status_reason()`, update switch statement (10 occurrences) |
| `src/http/ngx_http_special_response.c` | UPDATE | `src/http/ngx_http_special_response.c` | Integrate error page generation with status API (1 occurrence) |
| `src/http/ngx_http_upstream.c` | UPDATE | `src/http/ngx_http_upstream.c` | Add conditional validation bypass for proxied responses (2 occurrences) |
| `src/http/ngx_http_variables.c` | REFERENCE | `src/http/ngx_http_variables.c` | No changes needed - read-only `$status` variable extraction |

**HTTP Modules - Content Handlers:**

| Target File | Transformation | Source File | Key Changes |
|-------------|---------------|-------------|-------------|
| `src/http/modules/ngx_http_static_module.c` | UPDATE | `src/http/modules/ngx_http_static_module.c` | Migrate `r->headers_out.status = NGX_HTTP_OK` to API call |
| `src/http/modules/ngx_http_index_module.c` | UPDATE | `src/http/modules/ngx_http_index_module.c` | Migrate status assignments for index file handling |
| `src/http/modules/ngx_http_autoindex_module.c` | UPDATE | `src/http/modules/ngx_http_autoindex_module.c` | Migrate 200 OK assignment for directory listing |
| `src/http/modules/ngx_http_dav_module.c` | UPDATE | `src/http/modules/ngx_http_dav_module.c` | Migrate WebDAV operation status codes |
| `src/http/modules/ngx_http_flv_module.c` | UPDATE | `src/http/modules/ngx_http_flv_module.c` | Migrate FLV streaming status |
| `src/http/modules/ngx_http_mp4_module.c` | UPDATE | `src/http/modules/ngx_http_mp4_module.c` | Migrate MP4 streaming status |
| `src/http/modules/ngx_http_gzip_static_module.c` | UPDATE | `src/http/modules/ngx_http_gzip_static_module.c` | Migrate pre-compressed file serving status |
| `src/http/modules/ngx_http_stub_status_module.c` | UPDATE | `src/http/modules/ngx_http_stub_status_module.c` | Migrate stub status 200 response |
| `src/http/modules/ngx_http_empty_gif_module.c` | UPDATE | `src/http/modules/ngx_http_empty_gif_module.c` | Migrate empty GIF 200 response |
| `src/http/modules/ngx_http_random_index_module.c` | UPDATE | `src/http/modules/ngx_http_random_index_module.c` | Migrate random index status |

**HTTP Modules - Upstream/Gateway:**

| Target File | Transformation | Source File | Key Changes |
|-------------|---------------|-------------|-------------|
| `src/http/modules/ngx_http_proxy_module.c` | UPDATE | `src/http/modules/ngx_http_proxy_module.c` | Preserve upstream status pass-through, no validation on proxied responses |
| `src/http/modules/ngx_http_fastcgi_module.c` | UPDATE | `src/http/modules/ngx_http_fastcgi_module.c` | Preserve upstream status pass-through behavior |
| `src/http/modules/ngx_http_uwsgi_module.c` | UPDATE | `src/http/modules/ngx_http_uwsgi_module.c` | Preserve upstream status pass-through behavior |
| `src/http/modules/ngx_http_scgi_module.c` | UPDATE | `src/http/modules/ngx_http_scgi_module.c` | Preserve upstream status pass-through behavior |
| `src/http/modules/ngx_http_grpc_module.c` | UPDATE | `src/http/modules/ngx_http_grpc_module.c` | Preserve gRPC upstream status handling |
| `src/http/modules/ngx_http_memcached_module.c` | UPDATE | `src/http/modules/ngx_http_memcached_module.c` | Preserve memcached status handling |

**HTTP Modules - Access Control & Authentication:**

| Target File | Transformation | Source File | Key Changes |
|-------------|---------------|-------------|-------------|
| `src/http/modules/ngx_http_access_module.c` | UPDATE | `src/http/modules/ngx_http_access_module.c` | Migrate 403 Forbidden status assignment |
| `src/http/modules/ngx_http_auth_basic_module.c` | UPDATE | `src/http/modules/ngx_http_auth_basic_module.c` | Migrate 401 Unauthorized status |
| `src/http/modules/ngx_http_auth_request_module.c` | UPDATE | `src/http/modules/ngx_http_auth_request_module.c` | Migrate authentication subrequest status (2 occurrences) |
| `src/http/modules/ngx_http_limit_req_module.c` | UPDATE | `src/http/modules/ngx_http_limit_req_module.c` | Migrate 429 Too Many Requests status |
| `src/http/modules/ngx_http_limit_conn_module.c` | UPDATE | `src/http/modules/ngx_http_limit_conn_module.c` | Migrate 503 Service Unavailable status |

**HTTP Modules - URL Rewriting:**

| Target File | Transformation | Source File | Key Changes |
|-------------|---------------|-------------|-------------|
| `src/http/modules/ngx_http_rewrite_module.c` | UPDATE | `src/http/modules/ngx_http_rewrite_module.c` | Migrate `return` directive status handling (preserve validation in 100-999 range for backward compat) |

**HTTP Modules - Filter Chain:**

| Target File | Transformation | Source File | Key Changes |
|-------------|---------------|-------------|-------------|
| `src/http/modules/ngx_http_gzip_filter_module.c` | UPDATE | `src/http/modules/ngx_http_gzip_filter_module.c` | Status comparison checks preserved (3 occurrences) |
| `src/http/modules/ngx_http_chunked_filter_module.c` | UPDATE | `src/http/modules/ngx_http_chunked_filter_module.c` | Status checks for chunked encoding (4 occurrences) |
| `src/http/modules/ngx_http_range_filter_module.c` | UPDATE | `src/http/modules/ngx_http_range_filter_module.c` | Range request status handling (4 occurrences) |
| `src/http/modules/ngx_http_slice_filter_module.c` | UPDATE | `src/http/modules/ngx_http_slice_filter_module.c` | Slice filter status handling (6 occurrences) |
| `src/http/modules/ngx_http_not_modified_filter_module.c` | UPDATE | `src/http/modules/ngx_http_not_modified_filter_module.c` | 304 Not Modified handling (3 occurrences) |
| `src/http/modules/ngx_http_headers_filter_module.c` | UPDATE | `src/http/modules/ngx_http_headers_filter_module.c` | Header manipulation status checks (2 occurrences) |
| `src/http/modules/ngx_http_charset_filter_module.c` | UPDATE | `src/http/modules/ngx_http_charset_filter_module.c` | Charset conversion conditions (2 occurrences) |
| `src/http/modules/ngx_http_ssi_filter_module.c` | UPDATE | `src/http/modules/ngx_http_ssi_filter_module.c` | SSI processing status (1 occurrence) |
| `src/http/modules/ngx_http_xslt_filter_module.c` | UPDATE | `src/http/modules/ngx_http_xslt_filter_module.c` | XSLT transformation status (1 occurrence) |
| `src/http/modules/ngx_http_image_filter_module.c` | UPDATE | `src/http/modules/ngx_http_image_filter_module.c` | Image filter status (2 occurrences) |
| `src/http/modules/ngx_http_addition_filter_module.c` | UPDATE | `src/http/modules/ngx_http_addition_filter_module.c` | Addition filter conditions (1 occurrence) |

**HTTP/2 Protocol Files:**

| Target File | Transformation | Source File | Key Changes |
|-------------|---------------|-------------|-------------|
| `src/http/v2/ngx_http_v2_filter_module.c` | UPDATE | `src/http/v2/ngx_http_v2_filter_module.c` | Use `ngx_http_status_reason()` for HPACK encoding (4 occurrences) |

**HTTP/3 Protocol Files:**

| Target File | Transformation | Source File | Key Changes |
|-------------|---------------|-------------|-------------|
| `src/http/v3/ngx_http_v3_filter_module.c` | UPDATE | `src/http/v3/ngx_http_v3_filter_module.c` | Use `ngx_http_status_reason()` for QPACK encoding (9 occurrences) |

**Build System Files:**

| Target File | Transformation | Source File | Key Changes |
|-------------|---------------|-------------|-------------|
| `auto/options` | UPDATE | `auto/options` | Add `HTTP_STATUS_VALIDATION=NO` default and `--with-http_status_validation` flag |
| `auto/modules` | UPDATE | `auto/modules` | Add conditional compilation for validation mode |

**Documentation Files:**

| Target File | Transformation | Source File | Key Changes |
|-------------|---------------|-------------|-------------|
| `docs/api/status_codes.md` | CREATE | N/A | API reference documentation for new functions |
| `docs/migration/status_code_api.md` | CREATE | N/A | Migration guide for third-party module developers |
| `docs/xml/nginx/changes.xml` | UPDATE | `docs/xml/nginx/changes.xml` | Add changelog entry for status code registry feature |

### 0.4.2 Cross-File Dependencies

**Import Statement Updates:**

No header file imports change as the new API functions are added to existing public headers (`ngx_http.h`). All modules already include this header via `<ngx_http.h>`.

**Configuration Updates:**

The `auto/options` file requires updates to handle the new configure flag:
- FROM: No status validation option
- TO: `--with-http_status_validation` enables strict RFC 9110 mode

### 0.4.3 Code Transformation Patterns

**Pattern 1: Direct Status Assignment Migration**

```c
// Before:
r->headers_out.status = NGX_HTTP_OK;

// After:
ngx_http_status_set(r, NGX_HTTP_OK);
```

**Pattern 2: Status Assignment with Error Handling**

```c
// Before:
r->headers_out.status = NGX_HTTP_NOT_FOUND;
return ngx_http_special_response_handler(r, NGX_HTTP_NOT_FOUND);

// After:
if (ngx_http_status_set(r, NGX_HTTP_NOT_FOUND) != NGX_OK) {
    return NGX_HTTP_INTERNAL_SERVER_ERROR;
}
return ngx_http_special_response_handler(r, NGX_HTTP_NOT_FOUND);
```

**Pattern 3: Status Comparison (No Change)**

```c
// These patterns remain unchanged:
if (r->headers_out.status == NGX_HTTP_OK) { ... }
if (r->headers_out.status >= NGX_HTTP_BAD_REQUEST) { ... }
```

**Pattern 4: Reason Phrase Lookup Migration**

```c
// Before (in ngx_http_header_filter_module.c):
status_line = &ngx_http_status_lines[status - 200];

// After:
status_line = ngx_http_status_reason(status);
```

### 0.4.4 Wildcard Pattern Summary

| Pattern | Transformation | Count |
|---------|---------------|-------|
| `src/http/*.c` | UPDATE | 8 files |
| `src/http/modules/*.c` | UPDATE | ~30 files |
| `src/http/v2/*.c` | UPDATE | 1 file |
| `src/http/v3/*.c` | UPDATE | 1 file |
| `auto/*` | UPDATE | 2 files |
| `docs/**/*.md` | CREATE | 2 files |
| `docs/xml/nginx/changes.xml` | UPDATE | 1 file |

### 0.4.5 One-Phase Execution

The entire refactor will be executed by Blitzy in ONE phase. All files listed above are included in a single coordinated transformation to ensure consistency and avoid partial migration states.


## 0.5 Dependency Inventory

### 0.5.1 Key Private and Public Packages

**External Dependencies (No Changes Required):**

| Package | Registry | Version | Purpose |
|---------|----------|---------|---------|
| OpenSSL | System/Source | 1.1.1+ or 3.x | SSL/TLS support (unchanged by refactor) |
| PCRE/PCRE2 | System/Source | PCRE 8.x / PCRE2 10.x | Regular expression support (unchanged) |
| zlib | System/Source | 1.2.x | Compression support (unchanged) |

**Build System Dependencies:**

| Tool | Version | Purpose |
|------|---------|---------|
| GCC | 4.8+ | C compiler (C99 support required) |
| Clang | 3.4+ | Alternative C compiler |
| GNU Make | 3.81+ | Build orchestration |
| POSIX Shell | Any | Configure script execution |

**No New External Dependencies:**

This refactor introduces no new external library dependencies. All changes are internal to the NGINX codebase using existing C standard library facilities.

### 0.5.2 Internal Module Dependencies

**Status Code Registry Dependencies:**

The new status code registry and API functions will be implemented in `src/http/ngx_http_request.c` and depend on:

| Internal Module | Dependency Type | Description |
|-----------------|-----------------|-------------|
| `ngx_http_core_module` | Compilation | Registry initialized during HTTP configuration parsing |
| `ngx_palloc` | Runtime | Pool allocation for error logging (existing patterns) |
| `ngx_log` | Runtime | Validation failure logging |
| `ngx_string` | Compile-time | `ngx_str_t` for reason phrase storage |

### 0.5.3 Import Refactoring

**No Import Changes Required:**

All modules already include `<ngx_http.h>` which will contain the new API function declarations. The existing include chain remains:

```c
// Existing pattern in all HTTP modules (preserved):
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>  // Contains new API declarations
```

### 0.5.4 External Reference Updates

**Configuration Files:**

| File Pattern | Update Type | Description |
|--------------|-------------|-------------|
| `auto/options` | Add variable | `HTTP_STATUS_VALIDATION=NO` default |
| `auto/configure` | No change | Existing configure flow handles new option |
| `auto/modules` | Add conditional | Compilation flag for validation mode |

**Documentation Files:**

| File Pattern | Update Type | Description |
|--------------|-------------|-------------|
| `docs/api/status_codes.md` | CREATE | New API reference |
| `docs/migration/status_code_api.md` | CREATE | Migration guide |
| `docs/xml/nginx/changes.xml` | UPDATE | Changelog entry |

**Build Files:**

| File | Update Type | Description |
|------|-------------|-------------|
| `auto/options` | UPDATE | Add configure flag |
| `auto/modules` | UPDATE | Conditional compilation |

### 0.5.5 Header File Structure

**Modified Headers:**

| Header | Additions |
|--------|-----------|
| `src/http/ngx_http.h` | `ngx_http_status_set()`, `ngx_http_status_validate()`, `ngx_http_status_reason()`, `ngx_http_status_is_cacheable()` function declarations |
| `src/http/ngx_http_request.h` | `NGX_HTTP_STATUS_CACHEABLE`, `NGX_HTTP_STATUS_CLIENT_ERROR`, `NGX_HTTP_STATUS_SERVER_ERROR`, `NGX_HTTP_STATUS_INFORMATIONAL` flag definitions |

**Unchanged Headers:**

All other headers remain unchanged, preserving ABI compatibility for third-party modules.

### 0.5.6 Runtime Dependency Analysis

**Worker Process Initialization:**

```
Configuration Parsing Phase:
├── HTTP block parsed
├── Status code registry populated (static array)
└── Registry becomes immutable

Worker Fork:
├── Registry data inherited (copy-on-write)
├── No additional initialization required
└── Thread-safe read-only access

Request Processing:
├── ngx_http_status_set() validates and assigns
├── ngx_http_status_reason() performs O(1) lookup
└── No cross-worker coordination needed
```

**Memory Layout:**

| Component | Location | Size | Lifecycle |
|-----------|----------|------|-----------|
| Registry array | .rodata section | ~1KB | Process lifetime |
| API functions | .text section | ~500 bytes | Process lifetime |
| Per-request overhead | None | 0 bytes | N/A |

### 0.5.7 Compatibility Matrix

**NGINX Module API Compatibility:**

| API | Status | Notes |
|-----|--------|-------|
| `NGX_MODULE_V1` | Unchanged | Module structure preserved |
| `ngx_http_output_filter` | Unchanged | Filter chain preserved |
| `ngx_http_top_header_filter` | Unchanged | Header filter chain preserved |
| `r->headers_out.status` | Preserved | Direct access still works |
| `NGX_HTTP_*` macros | Preserved | All constants remain valid |

**Third-Party Module Impact:**

- Modules using direct `r->headers_out.status` assignment: Continue to work
- Modules can optionally migrate to new API for validation benefits
- No forced migration timeline (backward compatibility maintained)


## 0.6 Scope Boundaries

### 0.6.1 Exhaustively In Scope

**Source Transformations (with trailing patterns):**

| Pattern | File Count | Description |
|---------|------------|-------------|
| `src/http/ngx_http.h` | 1 | Public API header - add function declarations |
| `src/http/ngx_http_request.h` | 1 | Request header - add flag definitions, preserve macros |
| `src/http/ngx_http_request.c` | 1 | Core implementation - add registry and API |
| `src/http/ngx_http_core_module.c` | 1 | Core module - migrate status assignments |
| `src/http/ngx_http_header_filter_module.c` | 1 | Header filter - use registry for reason lookup |
| `src/http/ngx_http_special_response.c` | 1 | Error pages - integrate with API |
| `src/http/ngx_http_upstream.c` | 1 | Upstream - conditional validation bypass |
| `src/http/modules/ngx_http_static_module.c` | 1 | Static serving - migrate status |
| `src/http/modules/ngx_http_proxy_module.c` | 1 | Proxy - preserve pass-through |
| `src/http/modules/ngx_http_fastcgi_module.c` | 1 | FastCGI - preserve pass-through |
| `src/http/modules/ngx_http_uwsgi_module.c` | 1 | uWSGI - preserve pass-through |
| `src/http/modules/ngx_http_scgi_module.c` | 1 | SCGI - preserve pass-through |
| `src/http/modules/ngx_http_grpc_module.c` | 1 | gRPC - preserve pass-through |
| `src/http/modules/ngx_http_memcached_module.c` | 1 | memcached - preserve pass-through |
| `src/http/modules/ngx_http_rewrite_module.c` | 1 | Rewrite - migrate return directive |
| `src/http/modules/ngx_http_index_module.c` | 1 | Index - migrate status |
| `src/http/modules/ngx_http_autoindex_module.c` | 1 | Auto-index - migrate 200 OK |
| `src/http/modules/ngx_http_access_module.c` | 1 | Access - migrate 403 |
| `src/http/modules/ngx_http_auth_basic_module.c` | 1 | Auth basic - migrate 401 |
| `src/http/modules/ngx_http_auth_request_module.c` | 1 | Auth request - migrate status |
| `src/http/modules/ngx_http_limit_req_module.c` | 1 | Limit req - migrate 429 |
| `src/http/modules/ngx_http_limit_conn_module.c` | 1 | Limit conn - migrate 503 |
| `src/http/modules/ngx_http_dav_module.c` | 1 | WebDAV - migrate status |
| `src/http/modules/ngx_http_flv_module.c` | 1 | FLV - migrate status |
| `src/http/modules/ngx_http_mp4_module.c` | 1 | MP4 - migrate status |
| `src/http/modules/ngx_http_gzip_static_module.c` | 1 | Gzip static - migrate status |
| `src/http/modules/ngx_http_stub_status_module.c` | 1 | Stub status - migrate 200 |
| `src/http/modules/ngx_http_empty_gif_module.c` | 1 | Empty GIF - migrate 200 |
| `src/http/modules/ngx_http_random_index_module.c` | 1 | Random index - migrate status |
| `src/http/modules/ngx_http_gzip_filter_module.c` | 1 | Gzip filter - status checks |
| `src/http/modules/ngx_http_chunked_filter_module.c` | 1 | Chunked filter - status checks |
| `src/http/modules/ngx_http_range_filter_module.c` | 1 | Range filter - status handling |
| `src/http/modules/ngx_http_slice_filter_module.c` | 1 | Slice filter - status handling |
| `src/http/modules/ngx_http_not_modified_filter_module.c` | 1 | 304 filter - status handling |
| `src/http/modules/ngx_http_headers_filter_module.c` | 1 | Headers filter - status checks |
| `src/http/modules/ngx_http_charset_filter_module.c` | 1 | Charset filter - status checks |
| `src/http/modules/ngx_http_ssi_filter_module.c` | 1 | SSI filter - status handling |
| `src/http/modules/ngx_http_xslt_filter_module.c` | 1 | XSLT filter - status handling |
| `src/http/modules/ngx_http_image_filter_module.c` | 1 | Image filter - status handling |
| `src/http/modules/ngx_http_addition_filter_module.c` | 1 | Addition filter - status checks |
| `src/http/v2/ngx_http_v2_filter_module.c` | 1 | HTTP/2 filter - reason lookup |
| `src/http/v3/ngx_http_v3_filter_module.c` | 1 | HTTP/3 filter - reason lookup |

**Build System Updates:**

| Pattern | Description |
|---------|-------------|
| `auto/options` | Add HTTP_STATUS_VALIDATION flag |
| `auto/modules` | Conditional compilation support |

**Documentation Updates:**

| Pattern | Description |
|---------|-------------|
| `docs/api/status_codes.md` | CREATE: API reference |
| `docs/migration/status_code_api.md` | CREATE: Migration guide |
| `docs/xml/nginx/changes.xml` | UPDATE: Changelog entry |

**Test Validation:**

| Pattern | Description |
|---------|-------------|
| nginx-tests suite | Post-build regression validation |
| wrk benchmarks | Performance validation (<2% latency increase) |
| valgrind analysis | Memory leak validation |

### 0.6.2 Explicitly Out of Scope

**User-Requested Exclusions:**

| Exclusion | Rationale |
|-----------|-----------|
| Event loop (`src/event/*`) | Outside HTTP status code refactor scope |
| Configuration parser (`src/core/ngx_conf_file.c`) | No directive changes required |
| Memory allocator (`src/core/ngx_palloc.c`) | No allocation changes |
| Stream module (`src/stream/*`) | HTTP-only refactor |
| Mail module (`src/mail/*`) | HTTP-only refactor |

**Behavioral Prohibitions:**

| Prohibition | Rationale |
|-------------|-----------|
| Caching mechanisms for status lookups | Direct array indexing sufficient |
| Status code transformations during proxying | Upstream codes pass through unchanged |
| Registry modifications post-initialization | Immutability ensures thread safety |
| Registry structure optimization beyond static array | Performance target achievable with simple approach |
| `error_page` directive parsing changes | Existing behavior preserved exactly |
| Thread-local status code storage | No threading model changes |
| Custom reason phrase generation | Registry lookup only |
| Status code aliasing | Not required for RFC compliance |

**Third-Party Module Files:**

| Pattern | Status |
|---------|--------|
| `src/http/modules/perl/*` | Out of scope (Perl embedding) |
| Any modules outside nginx core | Out of scope (third-party) |

**Subsystem Exclusions:**

| Subsystem | Location | Rationale |
|-----------|----------|-----------|
| QUIC transport | `src/event/quic/*` | Transport layer, not HTTP status |
| Core process model | `src/core/ngx_cycle.c` | Process lifecycle unchanged |
| Signal handling | `src/core/ngx_process*.c` | Signal handling unchanged |
| Shared memory | `src/core/ngx_shmem.c` | No shared memory for registry |

### 0.6.3 Boundary Conditions

**Upstream Response Handling:**

- Status codes from `proxy_pass`, `fastcgi_pass`, `uwsgi_pass`, `scgi_pass`, `grpc_pass`, and `memcached_pass` backends pass through without validation
- `r->upstream` presence indicates proxied response - bypass strict validation
- Existing `proxy_intercept_errors` behavior preserved exactly

**Module API Stability:**

- `NGX_MODULE_V1` interface unchanged
- Filter chain function signatures unchanged
- Third-party modules continue to work without modification
- Direct `r->headers_out.status` assignment remains functional

**Configuration Compatibility:**

- All existing nginx.conf configurations work without modification
- No new mandatory directives introduced
- `--with-http_status_validation` is optional compile-time flag


## 0.7 Refactoring Rules

### 0.7.1 User-Specified Requirements

The following rules are explicitly emphasized by the user and must be strictly followed:

**API Contract Rules:**

| Rule | Implementation |
|------|----------------|
| All status code operations flow through unified `ngx_http_status_set()` API | Migrate all 76 direct assignment occurrences |
| Response status codes validated against RFC 9110 HTTP Semantics | Validate range 100-599, log violations |
| Zero functional regression in existing nginx behavior | nginx-tests suite must pass |
| Performance impact below 2% latency overhead | Benchmark with wrk before/after |

**Backward Compatibility Rules:**

| Rule | Implementation |
|------|----------------|
| Preserve existing nginx.conf directive behavior | No configuration changes required |
| Maintain nginx module API stability (`NGX_MODULE_V1`) | No module structure changes |
| No modifications to event loop, config parser, memory allocator | Subsystems remain untouched |
| Third-party modules outside nginx core untouched | Only core modules refactored |
| Graceful upgrade compatibility | New binary with old config works |

**Code Quality Rules:**

| Rule | Implementation |
|------|----------------|
| Follow existing nginx coding conventions | `ngx_` prefix, snake_case, 4-space indent |
| Add function-level comments | Document purpose, parameters, return values |
| Include RFC section references | Comments reference RFC 9110 Section 15 |
| API implementations under 50 lines | Concise, focused functions |

### 0.7.2 Validation Mode Rules

**Standard Mode (Default):**

| Behavior | Description |
|----------|-------------|
| Range check only | Accept status codes 100-599 |
| Debug logging | Log suspicious codes at `NGX_LOG_DEBUG` |
| Full backward compatibility | Permissive validation |

**Strict Mode (`--with-http_status_validation`):**

| Behavior | Description |
|----------|-------------|
| RFC 9110 enforcement | Reject codes outside 100-599 |
| Warn logging | Log violations at `NGX_LOG_WARN` (production), `NGX_LOG_ERR` (debug) |
| Single final status | Validate one final status per request |
| 1xx validation | Informational codes only before final response |

### 0.7.3 Module Conversion Sequence

The user specifies mandatory conversion order with validation gates:

```
Phase 1: Core Infrastructure (Critical)
├── ngx_http_request.c (registry + API)
├── ngx_http.h (declarations)
├── ngx_http_request.h (flags)
└── Validation: Compilation succeeds in both modes

Phase 2: Core Module Migration (High)
├── ngx_http_core_module.c
├── ngx_http_request.c (remaining status)
├── ngx_http_static_module.c
└── Validation: nginx-tests passes

Phase 3: Extended Module Migration (Medium)
├── Upstream modules (proxy, fastcgi, uwsgi, scgi, grpc, memcached)
├── Rewrite, index, autoindex modules
├── Access control and auth modules
└── Validation: nginx-tests passes, wrk benchmark

Phase 4: Filter and Protocol Modules (Medium)
├── Filter modules (gzip, chunked, range, slice, etc.)
├── HTTP/2 and HTTP/3 filter modules
├── Header filter module
└── Validation: Full regression suite, performance benchmark
```

### 0.7.4 Testing Rules

**Mandatory Test Execution:**

| Test Type | Command | Pass Criteria |
|-----------|---------|---------------|
| nginx-tests | `prove -r t/` | All tests pass |
| Performance | `wrk -t4 -c100 -d30s` | <2% latency increase |
| Memory | `valgrind --leak-check=full` | Zero leaks in status code paths |
| Build | `./auto/configure && make` | Linux (gcc/clang), FreeBSD, macOS |

**Regression Test Priority:**

| Category | Priority | Description |
|----------|----------|-------------|
| Status code-related tests | Critical | Direct validation of refactored functionality |
| Error page tests | High | `error_page` directive behavior |
| Upstream status tests | High | Proxy pass-through verification |
| Access control tests | High | 401, 403 status codes |
| Rewrite tests | High | `return` directive status codes |

### 0.7.5 Error Handling Rules

**Validation Failure Protocol:**

| Step | Action |
|------|--------|
| 1 | `ngx_http_status_set()` returns `NGX_ERROR` for invalid status |
| 2 | Log error at `NGX_LOG_ERR` with request context and attempted status |
| 3 | Calling module falls back to `NGX_HTTP_INTERNAL_SERVER_ERROR` (500) |
| 4 | Request processing continues (fail gracefully, do not terminate) |

**Debug Logging Format:**

```
"http status set: %ui %V (valid|invalid)"
```

Example: `"http status set: 404 Not Found (valid)"`

### 0.7.6 Documentation Rules

**Required Documentation Artifacts:**

| Document | Format | Content |
|----------|--------|---------|
| API Reference | Markdown | Function signatures, parameters, return values, examples |
| Migration Guide | Markdown | Before/after patterns, step-by-step instructions |
| Changelog | XML | NGINX CHANGES format entry |

**API Documentation Standard:**

Each function must include:
- Function purpose (one-line description)
- Parameter descriptions with types
- Return value semantics
- Error conditions
- Usage example
- RFC 9110 compliance notes


## 0.8 References

### 0.8.1 Source Files Analyzed

**Core HTTP Infrastructure:**

| File Path | Analysis Purpose |
|-----------|------------------|
| `src/http/ngx_http.h` | Public API structure, `ngx_http_status_t` definition |
| `src/http/ngx_http_request.h` | Status code macro definitions (lines 79-145), `ngx_http_headers_out_t` structure |
| `src/http/ngx_http_request.c` | Request lifecycle, status assignment patterns |
| `src/http/ngx_http_core_module.c` | Core error handling, status assignments |
| `src/http/ngx_http_header_filter_module.c` | `ngx_http_status_lines[]` array, status serialization |
| `src/http/ngx_http_special_response.c` | Error page HTML generation, status mapping |
| `src/http/ngx_http_upstream.c` | Upstream response status pass-through |

**Module Files (Representative Sample):**

| File Path | Analysis Purpose |
|-----------|------------------|
| `src/http/modules/ngx_http_static_module.c` | Static file status assignment pattern |
| `src/http/modules/ngx_http_proxy_module.c` | Proxy module structure |
| `src/http/modules/ngx_http_rewrite_module.c` | `return` directive status handling |

**HTTP/2 and HTTP/3 Protocol Files:**

| File Path | Analysis Purpose |
|-----------|------------------|
| `src/http/v2/ngx_http_v2_filter_module.c` | HPACK status encoding |
| `src/http/v3/ngx_http_v3_filter_module.c` | QPACK status encoding |

**Build System Files:**

| File Path | Analysis Purpose |
|-----------|------------------|
| `auto/options` | Configure flag handling patterns |
| `auto/configure` | Build system entry point |

**Documentation Files:**

| File Path | Analysis Purpose |
|-----------|------------------|
| `docs/xml/nginx/changes.xml` | Changelog format and conventions |

### 0.8.2 Directories Explored

| Directory Path | Contents | Relevance |
|----------------|----------|-----------|
| `/` (root) | Repository overview, community files | Project structure |
| `src/` | Main source tree | Core implementation |
| `src/http/` | HTTP subsystem core | Primary refactoring target |
| `src/http/modules/` | Built-in HTTP modules | Module migration targets |
| `src/http/v2/` | HTTP/2 implementation | Protocol integration |
| `src/http/v3/` | HTTP/3 implementation | Protocol integration |
| `auto/` | Build configuration system | Build integration |
| `docs/` | Documentation assets | Changelog format |

### 0.8.3 Search Queries Executed

**Repository Searches:**

| Query | Purpose | Results |
|-------|---------|---------|
| `grep -r "r->headers_out.status"` | Find all status assignments | 76 occurrences across 28 files |
| `grep -n "NGX_HTTP_"` in `ngx_http_request.h` | Identify status code macros | Lines 79-145 |
| `find -name "*.c"` in `src/http/modules/` | Enumerate module files | 59 module files |

**Web Research Conducted:**

| Topic | Purpose |
|-------|---------|
| RFC 9110 HTTP Semantics status codes | Compliance requirements, valid ranges |
| Registry pattern best practices | Design pattern implementation guidance |
| C static lookup table patterns | Performance-optimized implementation |

### 0.8.4 External References

**Standards Documents:**

| Reference | Version | Relevance |
|-----------|---------|-----------|
| RFC 9110 | June 2022 | HTTP Semantics - Status code definitions (Section 15) |
| RFC 9111 | June 2022 | HTTP Caching - Cacheability by status code |
| RFC 9112 | June 2022 | HTTP/1.1 - Message syntax |
| RFC 9113 | June 2022 | HTTP/2 - HPACK encoding |
| RFC 9114 | June 2022 | HTTP/3 - QPACK encoding |

**NGINX Documentation:**

| Reference | Relevance |
|-----------|-----------|
| NGINX Development Guide | Module API conventions |
| NGINX Source Code | Implementation patterns |

### 0.8.5 User-Provided Attachments

No file attachments were provided for this project.

### 0.8.6 Technical Specification Sections Referenced

| Section | Purpose |
|---------|---------|
| 1.3 SCOPE | Confirmed HTTP subsystem boundaries |
| 2.1 FEATURE CATALOG | Identified affected features (F-001 through F-047) |

### 0.8.7 Key Metrics Summary

| Metric | Value |
|--------|-------|
| Total files requiring modification | ~45 |
| Total status assignment occurrences | 76 |
| Status code macro definitions | 35 |
| HTTP modules affected | 28 |
| New API functions | 4 |
| New documentation files | 2 |
| Build system files | 2 |
| Performance target | <2% latency increase |
| Memory overhead target | <1KB per worker |


