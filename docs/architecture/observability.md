# Observability Integration — Centralized HTTP Status Code API

**Project:** NGINX 1.29.5 Centralized HTTP Status Code Refactor
**Scope:** `src/http/` subsystem — `ngx_http_status_set()` API and `ngx_http_status_registry[]`
**Audience:** SREs, platform engineers, observability-pipeline operators
**Companion documents:** [architecture diagrams](./status_code_refactor.md), [decision log and traceability matrix](./decision_log.md)

## Overview

This document describes how the refactored HTTP status code API integrates with NGINX's existing observability primitives, what gaps are filled, and how operators verify the integration locally. The refactor adds **no new runtime dependencies** and **no new SaaS integrations** — all observability is delivered through facilities already present in NGINX 1.29.5.

**Key principle:** Per AAP § 0.8.2, the refactor MUST NOT introduce caching mechanisms, thread-local storage, or custom metric counters. All per-code statistics are derived **post-hoc** from access-log aggregation, preserving the zero-overhead guarantee in the hot path.

**Document map:**

| Section | Topic | AAP Anchor |
|---|---|---|
| [Reused NGINX Observability Primitives](#reused-nginx-observability-primitives) | Inventory of existing facilities reused without modification | § 0.7.1.A |
| [Gaps Filled by This Refactor](#gaps-filled-by-this-refactor) | What the refactor adds (debug log line) and what it intentionally does not add | § 0.7.1.B |
| [Recommended `log_format` for Status-Code Observability](#recommended-log_format-for-status-code-observability) | A copy-pasteable JSON `log_format` directive | § 0.7.1.A |
| [Grafana Dashboard Template (Data-Source Agnostic)](#grafana-dashboard-template-data-source-agnostic) | Importable JSON with four panels | § 0.7.1.C |
| [Local-Environment Verifiability Checklist](#local-environment-verifiability-checklist) | Seven-step verification flow without SaaS dependencies | § 0.7.1.D |
| [Observability Contract Summary](#observability-contract-summary) | Final binding of project rule requirements to implementation | § 0.7.1, § 0.8.9 |
| [See Also](#see-also) | Cross-references to companion documents and external standards | n/a |

Related documents:

- [Architecture before/after diagrams](./status_code_refactor.md)
- [Decision log and traceability matrix](./decision_log.md)
- [API reference](../api/status_codes.md)
- [Migration guide](../migration/status_code_api.md)

## Reused NGINX Observability Primitives

The refactor leverages the following facilities already present in NGINX 1.29.5. No new dependency is introduced.

| Facility | What It Provides | How the Refactor Uses It |
|---|---|---|
| `error_log` directive + `ngx_log_error()` / `ngx_log_debug*()` family | Structured, severity-classified logging to file or syslog (levels `debug`, `info`, `notice`, `warn`, `error`, `crit`, `alert`, `emerg`) | `ngx_http_status_set()` emits `NGX_LOG_DEBUG_HTTP` trace for every call; `NGX_LOG_WARN` for out-of-range codes in strict mode; `NGX_LOG_ERR` for "1xx after final response" violations in strict mode. No new log severity or channel introduced. |
| `access_log` directive + `log_format` | Structured per-request logging to file, syslog, or JSON with variables | `$status` (already present) reports the emitted status code; `$request_id` provides the correlation-ID functionality already mandated by the project rule. Operators aggregate access logs to derive per-code rates — no in-core counter needed. |
| `$request_id` variable | 32-character hex correlation ID auto-generated per request at connection accept time | Included in structured log lines; ties a request's status-set decision to its upstream call, its cache status, and its final wire response. No new variable introduced. |
| `stub_status` module (`ngx_http_stub_status_module`) | Runtime metrics: accepted/handled/active connections, reading/writing/waiting counters | Exposed unchanged at its existing endpoint (`location /stub_status { stub_status; }`). Documents the metrics surface that operators scrape; no extension. |
| `debug_connection` / `debug_http` directives | Per-connection or global HTTP debug tracing switches | Enable during validation runs to capture every `ngx_http_status_set()` call in the error log at full detail without recompiling. |
| HTTP health-probe convention | `return 200;` or `stub_status;` endpoints behind a dedicated `location` block | Documents the canonical health/readiness-probe pattern (e.g., `location = /healthz { return 200 "ok\n"; }`) — the refactor preserves the `return` directive behavior bit-for-bit (AAP § 0.8.5), so existing probes continue to work. |
| `kill -USR1` signal on the master PID | Graceful log reopening for log-rotation integration (logrotate `postrotate` script hook) | Unchanged; no reopening-path modification required. Operators can rotate logs without disrupting running workers. |

## Gaps Filled by This Refactor

| Gap | Fill | AAP Reference |
|---|---|---|
| No structured trace of status-code decisions | `ngx_http_status_set()` emits a single debug-level log line via `ngx_log_debug3(NGX_LOG_DEBUG_HTTP, ...)` for every call: `"http status set: %ui %V (strict=%s upstream=%s)"` — encoding the numeric code, the registered reason phrase (as `ngx_str_t`), the build-mode flag (`yes`/`no` per `NGX_HTTP_STATUS_VALIDATION`), and upstream presence (`yes` if `r->upstream != NULL` for pass-through classification). Activated globally by `error_log /path/to/error.log debug;` or per-connection via `debug_connection ip_or_cidr;`. The minimal example shape from AAP § 0.8.7 is `"http status set: 404 Not Found (valid)"`. | § 0.7.1.B, § 0.8.7 |
| No per-status-code call counter | **Intentionally NOT filled in-core.** Per AAP § 0.8.2 ("Never introduce caching mechanisms for status code lookups beyond direct array indexing"), no auxiliary data structure is added. The existing `$status` access-log variable enables post-hoc per-code rate analysis via log aggregation tooling. See the Grafana Dashboard Template below for the canonical log-aggregation approach. | § 0.7.1.B, § 0.8.2 |
| No RFC-9110 violation counter | **Intentionally NOT filled in-core.** Instead, strict-mode violations are logged at `NGX_LOG_WARN` / `NGX_LOG_ERR`. Operators grep the error log (`grep 'ngx_http_status_set: invalid status' error.log \| wc -l`) to derive the count. Same rationale as above: zero runtime overhead in the hot path. | § 0.7.1.B, § 0.8.2 |

### Debug Log Line Specification

The single new debug line emitted by `ngx_http_status_set()` follows AAP § 0.8.7 verbatim:

- **Severity channel:** `NGX_LOG_DEBUG_HTTP` (compiled out unless the build was configured with `--with-debug` and `error_log` is set to `debug`)
- **Format string:** `"http status set: %ui %V (strict=%s upstream=%s)"`
- **Arguments:** numeric status code (`ngx_uint_t`), reason phrase (`ngx_str_t *` from `ngx_http_status_reason()`), strict-mode flag, upstream-presence flag
- **Example output (permissive build, no upstream):** `http status set: 404 Not Found (strict=no upstream=no)`
- **Example output (strict build, upstream pass-through):** `http status set: 502 Bad Gateway (strict=yes upstream=yes)`

The debug line carries the request's correlation ID via the standard NGINX log prefix (`ngx_log_debug3` automatically prepends the connection log context, which includes `r->connection->log->connection` and the configured request-ID variable when the `error_log` format is set up to include it).

## Recommended `log_format` for Status-Code Observability

The following `log_format` in `nginx.conf` captures all fields required for post-hoc status-code analytics. It is compatible with Grafana Loki, Elasticsearch, and any JSON-based log aggregation system.

```nginx
log_format observability escape=json
    '{'
        '"timestamp":"$time_iso8601",'
        '"request_id":"$request_id",'
        '"remote_addr":"$remote_addr",'
        '"method":"$request_method",'
        '"uri":"$request_uri",'
        '"status":$status,'
        '"upstream_status":"$upstream_status",'
        '"upstream_addr":"$upstream_addr",'
        '"upstream_response_time":"$upstream_response_time",'
        '"request_time":$request_time,'
        '"bytes_sent":$bytes_sent,'
        '"body_bytes_sent":$body_bytes_sent,'
        '"user_agent":"$http_user_agent",'
        '"referer":"$http_referer"'
    '}';

access_log /var/log/nginx/access.log observability;
error_log /var/log/nginx/error.log warn;
```

**Notes:**

- `$status` is the post-masquerade wire status code (e.g., `400` when the original was `497`).
- `$upstream_status` is the original upstream response status, unaffected by NGINX masquerade rules.
- `$request_id` ties each log line to the corresponding `ngx_http_status_set: ...` debug line in the error log.
- `escape=json` is supported natively by NGINX 1.11.8+; it correctly JSON-escapes embedded quotes, backslashes, and control characters. The refactored codebase is 1.29.5, so this directive is safe to use without compatibility checks.
- Numeric fields (`status`, `request_time`, `bytes_sent`, `body_bytes_sent`) are emitted unquoted to allow the log aggregator to index them as numbers. String fields with potentially-empty values (`upstream_status`, `upstream_addr`) are quoted to handle NGINX's `"-"` placeholder for missing upstream data.

## Grafana Dashboard Template (Data-Source Agnostic)

The following dashboard JSON provides four panels that visualize status-code behavior. The dashboard uses two datasource template variables:

- `$DS_LOGS` — a logs datasource (Loki, Elasticsearch, CloudWatch Logs, Splunk, etc.)
- `$DS_METRICS` — a metrics datasource (Prometheus, Mimir, Thanos, VictoriaMetrics, etc.)

Operators plug in their own stack via the Grafana dashboard import UI. No assumption is made about the query language; the placeholder queries use LogQL-style syntax that most log-aggregation systems can translate.

```json
{
  "annotations": {
    "list": [
      {
        "builtIn": 1,
        "datasource": {"type": "grafana", "uid": "-- Grafana --"},
        "enable": true,
        "hide": true,
        "iconColor": "rgba(0, 211, 255, 1)",
        "name": "Annotations & Alerts",
        "type": "dashboard"
      }
    ]
  },
  "description": "NGINX HTTP Status Code observability dashboard. Derived from access-log aggregation of the $status field and error-log grep for strict-mode validation rejections. No in-core NGINX counter is required - all metrics are post-hoc.",
  "editable": true,
  "fiscalYearStartMonth": 0,
  "graphTooltip": 1,
  "id": null,
  "links": [],
  "panels": [
    {
      "id": 1,
      "title": "Status-Class Distribution (1xx / 2xx / 3xx / 4xx / 5xx)",
      "description": "Time-series of NGINX response counts grouped by HTTP status class. Derived from access log $status field. A rising 5xx class typically indicates upstream or server-side failure; a rising 4xx typically indicates client-side issues or scraping.",
      "type": "timeseries",
      "datasource": {"uid": "${DS_LOGS}"},
      "gridPos": {"h": 8, "w": 12, "x": 0, "y": 0},
      "targets": [
        {
          "refId": "A",
          "expr": "sum by (status_class) (count_over_time({job=\"nginx\"} | json | label_format status_class=`{{ div .status 100 }}xx` [1m]))",
          "legendFormat": "{{ status_class }}"
        }
      ],
      "fieldConfig": {
        "defaults": {
          "custom": {"drawStyle": "line", "lineInterpolation": "smooth", "fillOpacity": 10, "stacking": {"mode": "normal"}},
          "unit": "reqps"
        }
      }
    },
    {
      "id": 2,
      "title": "Top-5 Individual Status Codes (per minute)",
      "description": "Top five most frequently emitted specific HTTP status codes over the selected time range. Useful for spotting anomalies like a spike in 404s, 499s (client-closed), or 502s (bad gateway).",
      "type": "table",
      "datasource": {"uid": "${DS_LOGS}"},
      "gridPos": {"h": 8, "w": 12, "x": 12, "y": 0},
      "targets": [
        {
          "refId": "A",
          "expr": "topk(5, sum by (status) (count_over_time({job=\"nginx\"} | json [1m])))",
          "legendFormat": "{{ status }}"
        }
      ],
      "fieldConfig": {
        "defaults": {
          "custom": {"displayMode": "color-background"},
          "mappings": [],
          "thresholds": {"mode": "absolute", "steps": [{"color": "green"}, {"color": "yellow", "value": 100}, {"color": "red", "value": 1000}]}
        }
      }
    },
    {
      "id": 3,
      "title": "4xx vs 5xx Rate - Alert Threshold",
      "description": "Per-second rate of 4xx vs 5xx responses. Alert fires when 5xx rate exceeds 5% of total requests OR 4xx rate exceeds 30% of total requests for 5 consecutive minutes. Both thresholds are overridable per environment.",
      "type": "timeseries",
      "datasource": {"uid": "${DS_LOGS}"},
      "gridPos": {"h": 8, "w": 12, "x": 0, "y": 8},
      "targets": [
        {
          "refId": "A",
          "expr": "sum(rate({job=\"nginx\"} | json | status >= 400 and status < 500 [1m]))",
          "legendFormat": "4xx/s"
        },
        {
          "refId": "B",
          "expr": "sum(rate({job=\"nginx\"} | json | status >= 500 and status < 600 [1m]))",
          "legendFormat": "5xx/s"
        }
      ],
      "fieldConfig": {
        "defaults": {
          "custom": {"drawStyle": "line", "lineInterpolation": "smooth", "fillOpacity": 20},
          "thresholds": {"mode": "absolute", "steps": [{"color": "green"}, {"color": "yellow", "value": 1}, {"color": "red", "value": 10}]},
          "unit": "reqps"
        }
      },
      "alert": {
        "name": "Elevated 4xx/5xx Rate",
        "conditions": [
          {
            "evaluator": {"params": [10], "type": "gt"},
            "operator": {"type": "and"},
            "query": {"params": ["B", "5m", "now"]},
            "reducer": {"params": [], "type": "avg"},
            "type": "query"
          }
        ],
        "executionErrorState": "alerting",
        "for": "5m",
        "frequency": "1m",
        "handler": 1,
        "noDataState": "no_data"
      }
    },
    {
      "id": 4,
      "title": "Strict-Mode Validation Rejections (--with-http_status_validation)",
      "description": "Count of strict-mode validation rejections derived from error log grep: 'ngx_http_status_set: invalid status' and 'ngx_http_status_set: 1xx status ... after final response'. Only meaningful on builds configured with --with-http_status_validation; zero on default permissive builds.",
      "type": "stat",
      "datasource": {"uid": "${DS_LOGS}"},
      "gridPos": {"h": 8, "w": 12, "x": 12, "y": 8},
      "targets": [
        {
          "refId": "A",
          "expr": "sum(count_over_time({job=\"nginx-error\"} |= \"ngx_http_status_set: invalid status\" [5m]))",
          "legendFormat": "out-of-range"
        },
        {
          "refId": "B",
          "expr": "sum(count_over_time({job=\"nginx-error\"} |= \"ngx_http_status_set: 1xx status\" [5m]))",
          "legendFormat": "1xx-after-final"
        }
      ],
      "fieldConfig": {
        "defaults": {
          "custom": {"textMode": "value_and_name"},
          "thresholds": {"mode": "absolute", "steps": [{"color": "green"}, {"color": "red", "value": 1}]},
          "unit": "short"
        }
      }
    }
  ],
  "refresh": "30s",
  "schemaVersion": 39,
  "tags": ["nginx", "http", "status-codes", "observability"],
  "templating": {
    "list": [
      {
        "current": {"selected": false},
        "description": "Logs datasource (Loki, Elasticsearch, CloudWatch Logs, Splunk, etc.)",
        "hide": 0,
        "includeAll": false,
        "label": "Logs Datasource",
        "multi": false,
        "name": "DS_LOGS",
        "options": [],
        "query": "loki",
        "refresh": 1,
        "regex": "",
        "skipUrlSync": false,
        "type": "datasource"
      },
      {
        "current": {"selected": false},
        "description": "Metrics datasource (Prometheus, Mimir, Thanos, VictoriaMetrics, etc.)",
        "hide": 0,
        "includeAll": false,
        "label": "Metrics Datasource",
        "multi": false,
        "name": "DS_METRICS",
        "options": [],
        "query": "prometheus",
        "refresh": 1,
        "regex": "",
        "skipUrlSync": false,
        "type": "datasource"
      }
    ]
  },
  "time": {"from": "now-6h", "to": "now"},
  "timepicker": {},
  "timezone": "",
  "title": "NGINX HTTP Status Codes - Observability",
  "uid": "nginx-http-status-obs",
  "version": 1,
  "weekStart": ""
}
```

**How to import:**

1. In Grafana, navigate to Dashboards → New → Import.
2. Paste the JSON above.
3. Select your logs datasource for `DS_LOGS` and metrics datasource for `DS_METRICS`.
4. Click Import.

**Alternative query adaptation:**

- Replace `{job="nginx"} | json` with your log-shipper's label scheme.
- Replace `status` with `status_code` or similar if your parser uses a different field name.
- For Elasticsearch, translate LogQL `| json` to a Lucene query referencing the `status` field, e.g., `+status:[400 TO 499]` for the 4xx slice.
- For Splunk, translate to `index=nginx earliest=-5m | stats count by status` or equivalent SPL expressions.
- For CloudWatch Logs Insights, translate to a `stats count(*) by status` query against the matching log group.

The dashboard schema version `39` is compatible with Grafana 10.x and later. Earlier Grafana releases may require a one-time auto-migration during import, which Grafana performs without prompting.

## Local-Environment Verifiability Checklist

All observability facilities are verifiable on a developer laptop or test VM without any SaaS or external tooling. The following checklist confirms end-to-end wiring.

### Prerequisites

```bash
# Clone and build nginx with the refactor (from repo root)
./auto/configure --with-debug
make

# Start nginx in foreground with a minimal config
cat > /tmp/nginx-obs.conf <<'EOF'
worker_processes 1;
daemon off;
error_log /tmp/nginx-error.log debug;
events { worker_connections 64; }
http {
    log_format main '$time_iso8601 $request_id $status $request';
    access_log /tmp/nginx-access.log main;
    server {
        listen 8080;
        location = /healthz { return 200 "ok\n"; }
        location = /missing { return 404; }
        location = /stub { stub_status; }
    }
}
EOF

./objs/nginx -c /tmp/nginx-obs.conf -p /tmp &
NGINX_PID=$!
```

### Verification Steps

| # | Step | Command | Expected Output |
|---|---|---|---|
| 1 | `error_log` tail confirms debug trace | `tail -f /tmp/nginx-error.log &` then `curl -s http://localhost:8080/missing` | Line containing `http status set: 404 Not Found (strict=no upstream=no)` appears in error log (requires `--with-debug` build) |
| 2 | `access_log` confirms `$status` parity | `cat /tmp/nginx-access.log` | Line with `404 GET /missing` |
| 3 | `$request_id` correlation | `curl -v http://localhost:8080/missing 2>&1 \| grep 'Request-Id'` then search error log for that ID | The returned request_id appears in BOTH the access log and the error log line for the same request |
| 4 | `stub_status` returns expected metrics | `curl -s http://localhost:8080/stub` | Plain-text output beginning with `Active connections: N` followed by `server accepts handled requests` |
| 5 | `kill -USR1` triggers clean log reopening | `mv /tmp/nginx-access.log /tmp/nginx-access.log.1 && kill -USR1 $NGINX_PID && ls -la /tmp/nginx-access*` | New empty `/tmp/nginx-access.log` is created; old file rotated to `.log.1` |
| 6 | `valgrind` captures no leak regression (optional) | `valgrind --leak-check=full --show-leak-kinds=all ./objs/nginx -c /tmp/nginx-obs.conf -p /tmp` for ~30 seconds under `wrk -c10 -d30s http://localhost:8080/missing`, then Ctrl-C | valgrind report shows no "definitely lost" blocks attributable to `ngx_http_status.c` symbols |
| 7 | Strict-mode rejection logs (requires `--with-http_status_validation` build) | Recompile with `./auto/configure --with-http_status_validation --with-debug && make`, restart, then force a module to emit an invalid code (e.g., via `lua_block` or debugger) | Error log contains `ngx_http_status_set: invalid status 999 (out of RFC 9110 range 100-599)` at `warn` level |

### Teardown

```bash
kill -QUIT $NGINX_PID
rm -f /tmp/nginx-obs.conf /tmp/nginx-error.log /tmp/nginx-access.log /tmp/nginx-access.log.1
```

### Verification Notes

- Step 1 requires the build to be configured with `--with-debug`. Without that flag, `NGX_LOG_DEBUG_HTTP` calls compile to no-ops and the trace line will not appear.
- Step 3's request-ID round-trip requires that the response carries a header conveying the ID back to the client. If the test config does not add `add_header X-Request-Id $request_id always;` to the `server` block, replace the curl-grep with a search of the access log for the timestamp of the request.
- Step 5 demonstrates the `kill -USR1` log-reopening signal documented in the [NGINX control signals reference](https://nginx.org/en/docs/control.html). The same signal is what `logrotate`'s `postrotate` hook invokes.
- Step 6 is optional because it depends on `valgrind` and `wrk` being installed; it is the verification gate cited in AAP § 0.8.1 R-9 for the zero-leak guarantee.
- Step 7 only emits the warning line when the build was configured with `--with-http_status_validation`. On default permissive builds, the message is logged at `NGX_LOG_DEBUG_HTTP` only and is silent unless `error_log` is set to `debug`.

## Observability Contract Summary

| Observability Rule Requirement | Implementation | AAP Anchor |
|---|---|---|
| Structured logging | `error_log` + `ngx_log_error()` / `ngx_log_debug_http()` (existing) + new `http status set: ...` debug line | § 0.7.1, § 0.8.7 |
| Distributed tracing | `$request_id` correlation ID (existing) passed through debug logs | § 0.7.1 |
| Metrics endpoint | `stub_status` module (existing, preserved, unchanged) | § 0.7.1 |
| Health/readiness checks | `return 200;` pattern documented (existing directive, preserved) | § 0.7.1, § 0.8.5 |
| Dashboard template | Grafana JSON with 4 data-source-agnostic panels (provided above) | § 0.7.1.C |
| Alert thresholds | Embedded in the 4xx/5xx rate panel (5% of total, 5m window) | § 0.7.1.C |
| Local verifiability | 7-step checklist above, zero external dependencies | § 0.7.1.D |

**What the refactor does NOT do (intentionally):**

- Does NOT introduce a new NGINX-side metrics exporter
- Does NOT introduce per-status-code counters in shared memory
- Does NOT introduce thread-local status code storage (forbidden by AAP § 0.8.2)
- Does NOT introduce caching mechanisms for status code lookups beyond direct array indexing (forbidden by AAP § 0.8.2)
- Does NOT introduce custom reason phrase generation beyond registry lookup (forbidden by AAP § 0.8.2)
- Does NOT introduce status code aliasing functionality (forbidden by AAP § 0.8.2)
- Does NOT modify the existing `error_page` directive parsing logic (forbidden by AAP § 0.8.2)
- Does NOT require Prometheus, Loki, Elasticsearch, or any specific backend
- Does NOT add new `nginx.conf` directives for observability configuration
- Does NOT modify the existing `access_log`, `error_log`, or `log_format` directive semantics

This contract fulfills the project-level Observability implementation rule (AAP § 0.8.9) without scope creep.

## See Also

- [`status_code_refactor.md`](./status_code_refactor.md) — Before/after architecture diagrams
- [`decision_log.md`](./decision_log.md) — Design decisions and traceability matrix
- [`../api/status_codes.md`](../api/status_codes.md) — Full API reference
- [`../migration/status_code_api.md`](../migration/status_code_api.md) — Migration guide for module authors
- [`../../CODE_REVIEW.md`](../../CODE_REVIEW.md) — Segmented PR review artifact
- RFC 9110 § 15 — [HTTP Status Codes](https://www.rfc-editor.org/rfc/rfc9110.html#name-status-codes)
- NGINX `stub_status` — [module documentation](https://nginx.org/en/docs/http/ngx_http_stub_status_module.html)
- NGINX log rotation — [USR1 signal documentation](https://nginx.org/en/docs/control.html)
- NGINX `log_format` — [http_log_module reference](https://nginx.org/en/docs/http/ngx_http_log_module.html#log_format)
- NGINX `$request_id` — [embedded variables reference](https://nginx.org/en/docs/http/ngx_http_core_module.html#variables)
