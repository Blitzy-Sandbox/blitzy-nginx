# Technical Specification

# 0. Agent Action Plan

## 0.1 Intent Clarification

### 0.1.1 Core Documentation Objective

Based on the provided requirements, the Blitzy platform understands that the documentation objective is to **create a new, comprehensive development archaeology and execution intelligence report** for the `reverse proxy and load balancing` subsystem of NGINX — a ~3,000–5,000 word structured narrative document targeting engineering leadership (CTOs, VPs of Engineering, Heads of Execution, Engineering Managers).

- **Category:** Create new documentation
- **Documentation type:** Execution Intelligence Report / Technical Archaeology Narrative
- **Audience:** Non-technical-depth engineering leadership — prioritizing actionable insight over raw technical detail
- **Core question each section must answer:** *"What does this tell us about how our team executes?"*

The report is structured around **10 sequential directives**, each building upon prior findings:

- **Directive 1 — Feature Boundary Discovery:** Autonomously discover all files, modules, configurations, and infrastructure comprising the reverse proxy and load balancing subsystem via keyword fan-out, dependency tracing, commit message mining, and boundary validation. Produce a Feature Manifest grouped by component/responsibility.
- **Directive 2 — Contributor Map:** Build a per-contributor profile across every manifest file — commit count, lines changed, date range, components touched, commit type classification, and bus factor role. Identify original authors, current maintainers, and knowledge silos.
- **Directive 3 — Delivery Timeline:** Reconstruct a chronological timeline with inception, major milestones, releases, pace changes, and handoffs. Compute six quantitative metrics: feature age, active development time, dormancy ratio, time to first integration, average commit cadence, and longest gap.
- **Directive 4 — Design Decisions & Technical Debt:** Catalog deliberate design tradeoffs with citations and every `TODO`/`FIXME`/`HACK`/`XXX` in current HEAD within manifest files, recording file:line, age, author, and acknowledgment status.
- **Directive 5 — State/Workflow Evolution:** Trace modifications to any state machine, workflow engine, or lifecycle model from introduction to HEAD; produce a text-based diagram of the current state/workflow.
- **Directive 6 — Execution Bottlenecks & Delivery Risks:** Analyze velocity blockers (stalls, thrashing, reverts), review/merge friction, collaboration patterns, and scope/commitment signals. Classify each bottleneck.
- **Directive 7 — Bug Archaeology & Quality Signal Analysis:** Search for bug-fix commits, catalog defensive code patterns, compute quality metrics (total bug-fix commits, bug-fix ratio, average time-to-fix, recurrence hotspots, open issues).
- **Directive 8 — Integration Surface & Maturity:** Document integration points with other subsystems, direction, coupling strength, and maturity classification (Production/Implemented/Stubbed/Designed/Absent).
- **Directive 9 — Execution Health Scorecard:** Synthesize Directives 2–8 into a six-dimension scorecard with 🔴🟡🟢 ratings and three prioritized recommendations.
- **Directive 10 — Archaeological Narrative:** Compile everything into a 12-section structured narrative document with strict citation and formatting rules.

### 0.1.2 Special Instructions and Constraints

- **Citation requirement:** Every factual claim must have a citation (commit hash, file:line, or explicit "not recorded in-tree")
- **Zero unattributed speculation:** Inferences must be labeled as `[inference]`
- **Metrics in tables, not buried in prose**
- **Narrative voice:** The document reads as a story of how this feature was built — not a data dump
- **Pass/fail criteria:** Each directive has explicit pass/fail conditions that must be satisfied
- **Minimum length:** ≥2,500 words in the final compiled document
- **All 12 sections in Directive 10 must be present in the final output**

**User-specified implementation rules that apply:**
- **Observability:** The report must document observable patterns — logging, tracing, metrics, health check patterns found in the codebase
- **Onboarding & Continued Development:** Findings must inform onboarding documentation quality assessment
- **Executive Presentation:** A reveal.js HTML artifact must accompany the report as a leadership-ready summary
- **Explainability:** Every non-trivial decision in methodology must be documented with rationale
- **Visual Architecture Documentation:** All visual elements must use Mermaid diagrams; before/after states shown where applicable

### 0.1.3 Technical Interpretation

These documentation requirements translate to the following technical documentation strategy:

- To **discover the feature boundary** (Directive 1), we will analyze `git log`, `grep`, import tracing, and commit message mining across the NGINX repository to produce a validated Feature Manifest of ~25 files spanning ≥6 distinct components
- To **build the contributor map** (Directive 2), we will run `git log --follow --all` with `--numstat` across every manifest file, compute per-contributor statistics, and classify bus factor roles
- To **reconstruct the delivery timeline** (Directive 3), we will create a chronological event log from first commit (2003-04-14) to HEAD (2025-11-30) and compute all six quantitative metrics
- To **analyze design decisions** (Directive 4), we will mine commit messages, code comments, and TODOs across all manifest files in current HEAD
- To **map state/workflow evolution** (Directive 5), we will trace the upstream request state machine in `ngx_http_upstream.c` across its 508-commit history
- To **identify execution bottlenecks** (Directive 6), we will analyze dormancy gaps, contributor handoffs, file churn rates, and revert history
- To **perform bug archaeology** (Directive 7), we will search for fix/bug/crash commits and compute quality metrics per component
- To **assess integration maturity** (Directive 8), we will map all callers of the upstream API and classify each integration point
- To **generate the scorecard** (Directive 9), we will synthesize all quantitative and qualitative findings into a six-dimension leadership dashboard
- To **compile the narrative** (Directive 10), we will create `docs/reports/reverse-proxy-archaeology-report.md` containing all 12 sections

### 0.1.4 Inferred Documentation Needs

- Based on repository analysis: The NGINX codebase has **no existing archaeology or execution intelligence documentation** — this is entirely new documentation
- Based on structure: The reverse proxy subsystem spans **6+ distinct component groups** across `src/http/`, `src/stream/`, `src/event/`, `conf/`, `auto/`, and `docs/` — requiring consolidated cross-component analysis
- Based on implementation rules: An **executive presentation artifact** (reveal.js HTML) is required alongside the narrative report
- Based on user rules: A **decision log** (Markdown table) documenting methodology choices is required per the Explainability rule
- Based on user rules: **Mermaid diagrams** are required for architecture visualization, contributor timelines, and state/workflow evolution

## 0.2 Documentation Discovery and Analysis

### 0.2.1 Existing Documentation Infrastructure Assessment

Repository analysis reveals a **minimal in-tree documentation structure** with no existing archaeology, execution intelligence, or subsystem-specific narrative documentation.

**Documentation files found:**
- `README.md` — Primary onboarding/architecture overview; includes a Load Balancing section and reverse proxy mentions but no subsystem-level analysis
- `CONTRIBUTING.md` — Contribution workflow, PR hygiene, commit message format
- `SECURITY.md` — Security policy and coordinated disclosure
- `SUPPORT.md` — Triage guide for Issues vs. Discussions
- `CODE_OF_CONDUCT.md` — Contributor Covenant v2.1
- `docs/html/index.html` — Default NGINX welcome page (static HTML)
- `docs/html/50x.html` — Default error page (static HTML)
- `docs/man/nginx.8` — NGINX man page
- `docs/xml/nginx/changes.xml` — XML-formatted changelog
- `docs/xslt/changes.xslt` / `docs/xsls/changes.xsls` — XSLT transforms for changelog rendering

**Documentation generators/tools detected:** None. The repository uses no mkdocs, Sphinx, Docusaurus, or similar documentation frameworks. Documentation is either raw Markdown (`*.md`) or XML-based changelog generation (`docs/xml/` + `docs/xslt/`).

**API documentation tools:** None detected. No JSDoc, Doxygen, or similar code documentation generators are configured.

**Diagram tools:** None currently integrated. Mermaid diagrams will be introduced as part of this documentation effort per user implementation rules.

**Key finding:** There is **zero existing subsystem-level archaeological documentation** in the repository. The `README.md` provides high-level architecture context (master/worker model, module system, configuration) and a brief Load Balancing getting-started section, but no contributor analysis, delivery timeline, or execution intelligence content exists anywhere in-tree.

### 0.2.2 Repository Code Analysis for Documentation

**Search patterns employed for feature boundary discovery (Directive 1):**

- **Keyword fan-out:** Searched `proxy`, `upstream`, `load_balanc`, `balancer`, `round_robin`, `ip_hash`, `least_conn`, `keepalive`, `hash`, `random` across file names, directory names, code content, configuration files, CI pipelines, and documentation
- **Dependency tracing:** Followed `#include` directives from `ngx_http_upstream.h` to discover all tightly-coupled consumer modules (`ngx_http_proxy_module.c`, `ngx_http_fastcgi_module.c`, `ngx_http_grpc_module.c`, `ngx_http_scgi_module.c`, `ngx_http_uwsgi_module.c`, `ngx_http_memcached_module.c`)
- **Commit message mining:** Searched `git log --all --oneline` for `proxy`, `upstream`, `load balanc*`, `balancer`, `round robin`, `ip_hash`, `least_conn`, `keepalive` — yielding 792 related commits out of 8,518 total (9.3%)
- **Build system analysis:** Inspected `auto/options` and `auto/modules` for feature flags (`HTTP_PROXY`, `HTTP_UPSTREAM_*`, `STREAM_UPSTREAM_*`)

**Key directories examined:**
- `src/http/` — Core upstream framework and HTTP proxy module
- `src/http/modules/` — Load balancer modules and proxy gateway modules
- `src/stream/` — L4 (TCP/UDP) proxy and upstream framework
- `src/event/` — Event pipe and peer connection infrastructure
- `conf/` — Sample configuration with proxy_pass scaffold
- `auto/` — Build system module registration and feature flags
- `.github/workflows/` — CI/CD pipelines (buildbot.yml, check-pr.yml, f5_cla.yml)

**Related documentation found providing context:**
- `README.md` — Load Balancing section with `upstream` block examples
- `conf/nginx.conf` — Commented `proxy_pass http://127.0.0.1;` scaffold (line 58)
- `docs/xml/nginx/changes.xml` — Changelog entries for proxy/upstream features

### 0.2.3 Git Repository Statistics

| Metric | Value |
|--------|-------|
| Total repository commits | 8,518 |
| Proxy/upstream related commits | 792 (9.3%) |
| Repository date range | 2002-08-06 to 2026-01-13 |
| Feature date range (event_pipe.c → proxy_v2_module.c) | 2003-04-14 to 2025-11-30 |
| Unique contributors to feature files | 20 |
| Contributors with ≥3 commits | 11 |
| Total feature-related source lines (current HEAD) | ~28,500 LOC across 25 files |

### 0.2.4 Web Search Research Conducted

No external web search research was needed for this task. All findings are derived directly from the repository's git history, source code, and build system — ensuring 100% evidence-based reporting with in-tree citations. The existing tech spec sections (4.3 Reverse Proxy Workflow, 4.5 Stream Proxy Workflow, 1.1 Executive Summary) provide supplementary architectural context already documented.

## 0.3 Documentation Scope Analysis

### 0.3.1 Code-to-Documentation Mapping

The Feature Manifest for the reverse proxy and load balancing subsystem comprises **25 source files** grouped across **7 distinct component categories**:

**Component 1: Core Upstream Framework (src/http/)**
- `src/http/ngx_http_upstream.c` — 7,247 LOC, 508 commits, first: 2005-01-18, last: 2025-07-15
  - Central upstream request state machine, peer connect/send/read, header processing, retries, buffering, cache/store, upgrade tunneling, upstream variables
  - Current documentation: No in-tree API docs; 13 TODO comments in HEAD
- `src/http/ngx_http_upstream.h` — 459 LOC, contract definitions for all upstream-based modules
  - Current documentation: Header comments only
- `src/http/ngx_http_upstream_round_robin.c` — 1,042 LOC, 75 commits, default weighted round-robin balancer
- `src/http/ngx_http_upstream_round_robin.h` — 239 LOC, round-robin peer data structures

**Component 2: HTTP Proxy Module (src/http/modules/)**
- `src/http/modules/ngx_http_proxy_module.c` — 5,414 LOC, 283 commits, first: 2005-04-08, last: 2025-07-15
  - HTTP reverse proxy implementation: `proxy_pass`, `proxy_set_header`, redirect/cookie rewriting, cache/SSL integration
  - Current documentation: No API docs; extensive conditional compilation for `NGX_HTTP_CACHE`, `NGX_HTTP_SSL`, `NGX_HTTP_V2`
- `src/http/modules/ngx_http_proxy_module.h` — 127 LOC, shared proxy type definitions (introduced 2025-07-15)
- `src/http/modules/ngx_http_proxy_v2_module.c` — 4,160 LOC, 5 commits, first: 2018-03-17 (originally), refactored: 2025-07-15
  - HTTP/2 upstream framing, flow control, cache and buffering support
  - Contains 1 TODO: "we can retry non-idempotent requests" (line 1917)

**Component 3: HTTP Load Balancer Modules (src/http/modules/)**
- `src/http/modules/ngx_http_upstream_ip_hash_module.c` — 288 LOC, first: 2006-12-04
  - IP-based session persistence; hashes first 3 octets of IPv4
  - Contains 1 TODO: "cached" (line 162)
- `src/http/modules/ngx_http_upstream_least_conn_module.c` — 326 LOC, first: 2012-06-03
  - Minimum active connections with weighted selection
- `src/http/modules/ngx_http_upstream_hash_module.c` — 756 LOC, first: 2014-06-02
  - Generic hash key with optional consistent hashing (CRC32)
- `src/http/modules/ngx_http_upstream_random_module.c` — 531 LOC, first: 2018-06-15
  - Random selection with optional power-of-two-choices (P2C)
- `src/http/modules/ngx_http_upstream_keepalive_module.c` — 576 LOC, first: 2011-09-15
  - Upstream connection pooling and reuse
- `src/http/modules/ngx_http_upstream_zone_module.c` — 1,007 LOC, first: 2015-04-14
  - Shared memory zones for cross-worker upstream state

**Component 4: Stream (L4) Proxy & Upstream (src/stream/)**
- `src/stream/ngx_stream_proxy_module.c` — 2,683 LOC, 86 commits, first: 2015-04-20
  - TCP/UDP Layer 4 proxying with upstream selection, retries, rate limits, PROXY protocol, SSL
- `src/stream/ngx_stream_upstream.c` — 868 LOC, stream upstream configuration parsing
- `src/stream/ngx_stream_upstream.h` — 165 LOC, stream upstream type definitions
- `src/stream/ngx_stream_upstream_round_robin.c` — 1,075 LOC, stream round-robin balancer
- `src/stream/ngx_stream_upstream_round_robin.h` — stream round-robin data structures
- `src/stream/ngx_stream_upstream_hash_module.c` — stream hash balancer
- `src/stream/ngx_stream_upstream_least_conn_module.c` — stream least-conn balancer
- `src/stream/ngx_stream_upstream_random_module.c` — stream random balancer
- `src/stream/ngx_stream_upstream_zone_module.c` — stream shared memory zones

**Component 5: Event Layer Infrastructure (src/event/)**
- `src/event/ngx_event_pipe.c` — 1,146 LOC, 92 commits, first: 2003-04-14
  - Upstream↔downstream data transfer pump; buffering, temp-file spooling
  - Contains 2 TODOs: "free unused bufs" (line 590), "free buf if p->free_bufs" (line 721)
- `src/event/ngx_event_pipe.h` — event pipe data structures
- `src/event/ngx_event_connect.c` — 435 LOC, outbound peer connection establishment
  - Contains 1 TODO: Win32 workaround (line 282)
- `src/event/ngx_event_connect.h` — peer connection type definitions

**Component 6: Configuration (conf/)**
- `conf/nginx.conf` — Sample config with commented `proxy_pass http://127.0.0.1;` scaffold (line 58)

**Component 7: Build System (auto/)**
- `auto/options` — Feature flags: `HTTP_PROXY`, `HTTP_UPSTREAM_HASH`, `HTTP_UPSTREAM_IP_HASH`, `HTTP_UPSTREAM_LEAST_CONN`, `HTTP_UPSTREAM_RANDOM`, `HTTP_UPSTREAM_KEEPALIVE`, `HTTP_UPSTREAM_ZONE`, `STREAM_UPSTREAM_*`
- `auto/modules` — Module registration and source file linkage for all proxy/upstream modules

### 0.3.2 Documentation Gap Analysis

Given the requirements and repository analysis, documentation gaps for the archaeology report include:

- **No existing contributor analysis:** Zero in-tree documentation on who built the reverse proxy subsystem, contributor patterns, or bus factor risks
- **No delivery timeline documentation:** No milestones, velocity metrics, or dormancy analysis exists
- **No design decision records:** 13 TODO/FIXME comments in HEAD with no accompanying decision log or rationale documentation
- **No execution bottleneck documentation:** Dormancy gaps (up to 521 days between commits) are undocumented
- **No quality metrics:** Bug-fix ratios (39% on ngx_http_upstream.c, 48% on ngx_event_pipe.c) are not documented anywhere
- **No integration maturity matrix:** Cross-subsystem dependencies are implicit in code but not documented
- **No executive scorecard:** No leadership-facing health assessment exists for any NGINX subsystem

## 0.4 Documentation Implementation Design

### 0.4.1 Documentation Structure Planning

The archaeology report and its companion artifacts will follow this structure:

```
docs/
├── reports/
│   └── reverse-proxy-archaeology-report.md   (PRIMARY: 3,000–5,000 word narrative)
├── presentations/
│   └── reverse-proxy-executive-summary.html  (reveal.js executive presentation)
└── decision-logs/
    └── archaeology-report-decisions.md       (methodology decision log)
```

The primary report (`reverse-proxy-archaeology-report.md`) will contain all 12 sections specified in Directive 10:

```
reverse-proxy-archaeology-report.md
├── 1. Executive Summary (3–5 sentences)
├── 2. Feature Identity (manifest, scope)
├── 3. The Team (contributor map, bus factor)
├── 4. The Journey (delivery timeline, velocity)
├── 5. Design Decisions & Debt
├── 6. State/Workflow Evolution (with Mermaid diagram)
├── 7. Execution Bottlenecks (classified, cited)
├── 8. Quality & Bug Ledger (resolved + remaining)
├── 9. Integration Maturity Matrix
├── 10. Execution Health Scorecard (6 dimensions)
├── 11. Recommended Actions (3 prioritized)
└── 12. Open Questions
```

### 0.4.2 Content Generation Strategy

**Information Extraction Approach:**

- Extract feature manifest from `git log --follow --diff-filter=A` and `git log --numstat` across all 25 identified files
- Extract contributor profiles from `git log --format='%aN <%aE>'` with `--numstat` for lines changed
- Generate timeline milestones from `git log --reverse --oneline` with date extraction per component file
- Extract TODO/FIXME/HACK instances from `grep -rn` across manifest files in current HEAD
- Compute quality metrics from `git log --grep="fix|bug|segfault|crash"` with per-file counting
- Map integration surface from `grep -rn "ngx_http_upstream.h"` include analysis and `ngx_http_upstream_init` caller analysis

**Template Application:**

- Directive 10 specifies the exact 12-section structure — this serves as the mandatory template
- Each directive's pass/fail criteria define minimum content requirements per section
- The contributor map (Section 3) uses the format: commit count, lines changed, date range, components, commit types, bus factor role
- The delivery timeline (Section 4) includes a mandatory 6-row metrics table
- The quality ledger (Section 8) includes a mandatory 5-row quality metrics table
- The scorecard (Section 10) uses a mandatory 6-row dimension table with 🔴🟡🟢 ratings

**Documentation Standards:**

- Markdown formatting with `#` through `####` headers
- Mermaid diagrams for: upstream request state machine, contributor activity timeline, component dependency graph
- Code references using `Source: file:line` format
- Tables for all metrics, contributor profiles, integration matrices, and the scorecard
- Consistent terminology: "upstream" (backend server pool), "peer" (individual backend), "balancer" (selection algorithm), "pipe" (data transfer pump)

### 0.4.3 Diagram and Visual Strategy

**Mermaid diagrams to create:**

- **Component Dependency Graph** — Showing relationships between the 7 component groups (upstream core → proxy module → balancer modules → event pipe → stream proxy)
- **Contributor Activity Timeline** — Gantt-style chart showing each major contributor's active period and the handoff points
- **Upstream Request State Machine** — Current-state diagram of the `ngx_http_upstream.c` state machine (init → connect → send → recv headers → recv body → finalize)
- **Integration Surface Map** — Graph showing which subsystems call into the upstream API and their maturity level
- **Delivery Velocity Chart** — Annual commit counts showing acceleration and deceleration periods

All diagrams will include descriptive titles and legends per the Visual Architecture Documentation implementation rule. Where the feature has evolved (e.g., addition of HTTP/2 proxy support in 2025), before/after states will be shown.

## 0.5 Documentation File Transformation Mapping

### 0.5.1 File-by-File Documentation Plan

| Target Documentation File | Transformation | Source Code/Docs | Content/Changes |
|---------------------------|----------------|------------------|-----------------|
| docs/reports/reverse-proxy-archaeology-report.md | CREATE | src/http/ngx_http_upstream.c, src/http/modules/ngx_http_proxy_module.c, src/http/modules/ngx_http_proxy_v2_module.c, src/http/modules/ngx_http_upstream_*.c, src/stream/ngx_stream_proxy_module.c, src/stream/ngx_stream_upstream*.c, src/event/ngx_event_pipe.c, src/event/ngx_event_connect.c, auto/options, auto/modules, conf/nginx.conf, git log history | Complete 12-section Feature Archaeology & Execution Intelligence Report (~3,000–5,000 words). Sections: Executive Summary, Feature Identity (manifest), The Team (contributor map + bus factor), The Journey (timeline + 6 metrics), Design Decisions & Debt (≥3 decisions + all TODOs), State/Workflow Evolution (upstream state machine diagram), Execution Bottlenecks (≥3 classified), Quality & Bug Ledger (5 quality metrics), Integration Maturity Matrix (≥3 integration points), Execution Health Scorecard (6 dimensions 🔴🟡🟢), Recommended Actions (3 prioritized), Open Questions |
| docs/presentations/reverse-proxy-executive-summary.html | CREATE | docs/reports/reverse-proxy-archaeology-report.md | reveal.js HTML executive presentation summarizing: feature health, bus factor risks, velocity trends, quality signals, recommended actions. Every slide includes ≥1 visual element (Mermaid diagrams embedded). Scoped to findings from the archaeology report |
| docs/decision-logs/archaeology-report-decisions.md | CREATE | Methodology analysis during report creation | Decision log as Markdown table: what was decided, alternatives, rationale, risks. Covers: feature boundary methodology, commit classification heuristics, bus factor thresholds, quality metric definitions, bottleneck classification criteria |
| README.md | REFERENCE | README.md | Use existing README structure, Load Balancing section, and architecture overview as style/context reference for the archaeology report's Feature Identity section. No modifications to README itself |
| conf/nginx.conf | REFERENCE | conf/nginx.conf | Reference the commented proxy_pass scaffold (line 58) as evidence of configuration documentation patterns. No modifications |
| docs/xml/nginx/changes.xml | REFERENCE | docs/xml/nginx/changes.xml | Cross-reference changelog entries with delivery timeline milestones. No modifications |

### 0.5.2 New Documentation Files Detail

**File: docs/reports/reverse-proxy-archaeology-report.md**
- Type: Execution Intelligence Report / Archaeological Narrative
- Source Code: All 25 manifest files + full git history (8,518 commits, 792 feature-related)
- Sections:
  - Executive Summary — 3–5 sentence CTO-ready overview
  - Feature Identity — Feature Manifest table (25 files, 7 components, first/last commit dates)
  - The Team — Contributor profiles for 11 contributors with ≥3 commits; bus factor analysis per component
  - The Journey — Chronological timeline with ≥5 milestones; 6-metric quantitative table (feature age: ~22 years, active months: 221, dormancy ratio, cadence, longest gap: 521 days)
  - Design Decisions & Debt — ≥3 design decisions with citations; 13 TODO/FIXME entries cataloged with file:line, age (oldest: 2005-01-18), author
  - State/Workflow Evolution — Mermaid diagram of upstream request state machine (init → peer selection → connect → SSL handshake → send request → read response → buffer/stream → finalize)
  - Execution Bottlenecks — ≥3 bottlenecks classified as Blocked/Contested/Under-resourced/Abandoned/Deferred with commit hash citations
  - Quality & Bug Ledger — 5-metric quality table (345 bug-fix commits, 39% bug-fix ratio on upstream.c, recurrence hotspots in event_pipe.c at 48%)
  - Integration Maturity Matrix — ≥3 integration points (HTTP proxy → upstream, gRPC → upstream, FastCGI → upstream, stream proxy → stream upstream, memcached → upstream)
  - Execution Health Scorecard — 6-dimension table with 🔴🟡🟢 ratings
  - Recommended Actions — 3 prioritized recommendations with evidence citations
  - Open Questions — Unanswered questions for leadership investigation
- Diagrams: Component dependency graph, contributor timeline, upstream state machine, integration surface map
- Key Citations: Every factual claim cites commit hash, file:line, or "not recorded in-tree"

**File: docs/presentations/reverse-proxy-executive-summary.html**
- Type: reveal.js Executive Presentation
- Source: Synthesized from archaeology report findings
- Slides:
  - Title slide with feature identity
  - Execution Health Scorecard (embedded table)
  - Bus Factor Risk visualization (Mermaid diagram)
  - Delivery Velocity trend (Mermaid chart)
  - Quality Signal summary (metrics table)
  - Top 3 Recommended Actions
  - Open Questions for leadership
- Every slide includes ≥1 visual element (Mermaid diagrams, tables, or styled metrics)

**File: docs/decision-logs/archaeology-report-decisions.md**
- Type: Decision Log (Markdown Table)
- Content:
  - Feature boundary methodology: keyword fan-out + dependency tracing + commit mining vs. manual enumeration
  - Commit classification heuristic: grep-based message pattern matching for feature/bugfix/refactor
  - Bus factor thresholds: >80% = sole owner, >40% = primary, <5 commits = drive-by
  - Quality metric definitions: bug-fix ratio = bug-fix commits / total commits per file
  - Bottleneck classification criteria: stall = >2 months inactivity, thrashing = >5 modifications in 14 days

### 0.5.3 Documentation Configuration Updates

- No mkdocs.yml, docusaurus.config.js, or similar documentation framework configuration exists in the repository
- The new `docs/reports/` and `docs/presentations/` directories are self-contained and require no build system integration
- The reveal.js presentation is a standalone HTML file with embedded CDN references for the reveal.js framework and Mermaid rendering

### 0.5.4 Cross-Documentation Dependencies

- `docs/reports/reverse-proxy-archaeology-report.md` → referenced by `docs/presentations/reverse-proxy-executive-summary.html` (presentation summarizes report findings)
- `docs/decision-logs/archaeology-report-decisions.md` → referenced by the report's methodology sections
- `README.md` → provides context for Feature Identity section (existing Load Balancing documentation)
- No navigation links, table of contents updates, or index/glossary changes are required (new standalone documentation)

## 0.6 Dependency Inventory

### 0.6.1 Documentation Dependencies

No documentation framework packages are currently installed in the NGINX repository. The following tools and packages are relevant to this documentation exercise:

| Registry | Package Name | Version | Purpose |
|----------|--------------|---------|---------|
| CDN | reveal.js | 5.1.0 | Presentation framework for executive summary HTML artifact (loaded via CDN: `cdn.jsdelivr.net/npm/reveal.js@5.1.0`) |
| CDN | mermaid | 11.4.1 | Diagram rendering for Mermaid diagrams embedded in reveal.js presentation (loaded via CDN: `cdn.jsdelivr.net/npm/mermaid@11.4.1`) |
| system | git | 2.x (pre-installed) | Git history analysis for archaeology directives — required for `git log`, `git blame`, `git diff` operations |
| system | grep/awk/sed | POSIX (pre-installed) | Text processing for commit message mining, TODO extraction, and contributor analysis |

**Notes:**
- The primary report (`reverse-proxy-archaeology-report.md`) is pure Markdown and requires no build tools
- The decision log (`archaeology-report-decisions.md`) is pure Markdown and requires no build tools
- The reveal.js presentation uses CDN-hosted dependencies, requiring no local package installation
- All Mermaid diagrams in the Markdown report are rendered by the consumer's Markdown viewer (GitHub, VS Code, etc.)
- No package.json, requirements.txt, or other dependency manifest changes are required

### 0.6.2 Documentation Reference Updates

No existing documentation links require updating. The new files are standalone additions:

- `docs/reports/reverse-proxy-archaeology-report.md` — New file, no existing references to update
- `docs/presentations/reverse-proxy-executive-summary.html` — New file, no existing references to update
- `docs/decision-logs/archaeology-report-decisions.md` — New file, no existing references to update

The README.md Load Balancing section (which links to external `nginx.org/en/docs/` for detailed documentation) will not be modified. If future onboarding documentation updates are desired, a link to the archaeology report could be added — this is noted as a suggested next task per the Onboarding & Continued Development implementation rule.

## 0.7 Coverage and Quality Targets

### 0.7.1 Documentation Coverage Metrics

**Current coverage analysis (pre-implementation):**

| Coverage Dimension | Current State | Target |
|--------------------|---------------|--------|
| Feature boundary documentation | 0% — no manifest exists | 100% — all 25 files documented with component grouping |
| Contributor profiling | 0% — no contributor analysis | 100% — all 11 contributors with ≥3 commits profiled |
| Delivery timeline documentation | 0% — no timeline exists | 100% — ≥5 milestones, all 6 metrics computed |
| Design decision records | 0% — no decision log | ≥3 decisions documented with citations |
| Technical debt catalog | 0% — TODOs undocumented | 100% — all 13 TODO/FIXME entries cataloged |
| Bug/quality metrics | 0% — no quality analysis | 100% — 5-metric quality table with per-file ratios |
| Integration maturity assessment | 0% — no integration docs | 100% — ≥3 integration points with maturity classification |
| Executive scorecard | 0% — no leadership dashboard | 100% — 6-dimension scorecard with recommendations |
| Executive presentation | 0% — no presentation exists | 100% — reveal.js artifact with visual elements on every slide |
| Methodology decision log | 0% — no decision log | 100% — all non-trivial methodology decisions documented |

**Target coverage:** 100% of all Directive 1–10 pass/fail criteria satisfied.

### 0.7.2 Documentation Quality Criteria

**Completeness requirements:**
- All 12 sections from Directive 10 present in final report
- Every directive's pass/fail criteria explicitly satisfied
- Feature Manifest contains files across ≥3 distinct components (target: 7 achieved)
- ≥5 dated milestones in timeline (target: ≥13 identified from data)
- ≥3 bottlenecks identified with classification and citations
- ≥3 integration points assessed with maturity classification
- All 6 scorecard dimensions rated with evidence summaries
- 3 recommended actions present with specific evidence citations
- Document length ≥2,500 words

**Accuracy validation:**
- Every factual claim has a citation: commit hash (e.g., `02025fd6b`), file:line (e.g., `src/http/ngx_http_upstream.c:887`), or explicit "not recorded in-tree"
- Zero unattributed speculation — all inferences labeled as `[inference]`
- Contributor statistics verified against `git log --numstat` output
- TODO/FIXME entries verified against current HEAD via `grep -rn`
- Bug-fix ratios computed from actual commit counts (not estimates)

**Clarity standards:**
- Narrative voice — reads as a story, not a data dump
- Metrics presented in tables, not buried in prose
- Leadership-appropriate language — prioritizing actionable insight over raw technical detail
- Every section answers: "What does this tell us about how our team executes?"
- Progressive disclosure: executive summary first, details in subsequent sections

**Maintainability:**
- Source citations enable traceability back to specific commits and files
- Mermaid diagrams are text-based and version-controllable
- Report structure follows the 12-section template from Directive 10
- Decision log provides methodology traceability

### 0.7.3 Example and Diagram Requirements

| Requirement | Specification |
|-------------|---------------|
| Mermaid diagrams | ≥4 diagrams: component dependency, contributor timeline, state machine, integration surface |
| Metrics tables | ≥5 tables: delivery metrics (6 rows), quality metrics (5 rows), contributor profiles, integration matrix, scorecard (6 rows) |
| Commit hash citations | Every factual claim backed by ≥1 commit hash or file:line reference |
| Code example testing | Not applicable (documentation-only deliverable) |
| Visual content freshness | All diagrams reflect current HEAD state; before/after shown where applicable (per Visual Architecture Documentation rule) |

## 0.8 Scope Boundaries

### 0.8.1 Exhaustively In Scope

**New documentation files:**
- `docs/reports/reverse-proxy-archaeology-report.md` — Primary 3,000–5,000 word narrative
- `docs/presentations/reverse-proxy-executive-summary.html` — reveal.js executive presentation
- `docs/decision-logs/archaeology-report-decisions.md` — Methodology decision log

**Source files analyzed (read-only — no modifications):**
- `src/http/ngx_http_upstream.c` — Core upstream framework
- `src/http/ngx_http_upstream.h` — Upstream API contract
- `src/http/ngx_http_upstream_round_robin.c` — Default round-robin balancer
- `src/http/ngx_http_upstream_round_robin.h` — Round-robin data structures
- `src/http/modules/ngx_http_proxy_module.c` — HTTP reverse proxy
- `src/http/modules/ngx_http_proxy_module.h` — Proxy type definitions
- `src/http/modules/ngx_http_proxy_v2_module.c` — HTTP/2 proxy support
- `src/http/modules/ngx_http_upstream_hash_module.c` — Hash balancer
- `src/http/modules/ngx_http_upstream_ip_hash_module.c` — IP hash balancer
- `src/http/modules/ngx_http_upstream_least_conn_module.c` — Least connections balancer
- `src/http/modules/ngx_http_upstream_random_module.c` — Random balancer
- `src/http/modules/ngx_http_upstream_keepalive_module.c` — Keepalive connection pool
- `src/http/modules/ngx_http_upstream_zone_module.c` — Shared memory zones
- `src/stream/ngx_stream_proxy_module.c` — L4 TCP/UDP proxy
- `src/stream/ngx_stream_upstream.c` — Stream upstream config
- `src/stream/ngx_stream_upstream.h` — Stream upstream types
- `src/stream/ngx_stream_upstream_round_robin.c` — Stream round-robin
- `src/stream/ngx_stream_upstream_round_robin.h` — Stream round-robin types
- `src/stream/ngx_stream_upstream_hash_module.c` — Stream hash balancer
- `src/stream/ngx_stream_upstream_least_conn_module.c` — Stream least-conn
- `src/stream/ngx_stream_upstream_random_module.c` — Stream random balancer
- `src/stream/ngx_stream_upstream_zone_module.c` — Stream zones
- `src/event/ngx_event_pipe.c` — Data transfer pump
- `src/event/ngx_event_pipe.h` — Pipe data structures
- `src/event/ngx_event_connect.c` — Peer connection establishment
- `src/event/ngx_event_connect.h` — Peer connection types

**Configuration and build files analyzed (read-only):**
- `conf/nginx.conf` — Sample configuration with proxy scaffold
- `auto/options` — Feature flag definitions
- `auto/modules` — Module registration

**Git history analyzed:**
- Full repository history (8,518 commits) for commit mining, contributor profiling, timeline reconstruction
- 792 proxy/upstream-related commits analyzed in detail

**Integration surface files analyzed (read-only):**
- `src/http/modules/ngx_http_fastcgi_module.c` — FastCGI gateway (upstream consumer)
- `src/http/modules/ngx_http_grpc_module.c` — gRPC gateway (upstream consumer)
- `src/http/modules/ngx_http_scgi_module.c` — SCGI gateway (upstream consumer)
- `src/http/modules/ngx_http_uwsgi_module.c` — uWSGI gateway (upstream consumer)
- `src/http/modules/ngx_http_memcached_module.c` — Memcached protocol (upstream consumer)

### 0.8.2 Explicitly Out of Scope

- **Source code modifications** — No changes to any `.c`, `.h`, or build files; this is a documentation-only deliverable
- **Test file modifications** — No test changes (NGINX uses external `nginx-tests` repository, not in-tree)
- **Feature additions or code refactoring** — No code changes of any kind
- **Deployment configuration changes** — No changes to CI/CD, Docker, or infrastructure
- **Unrelated subsystem documentation** — HTTP/2, HTTP/3, QUIC, mail proxy, SSL/TLS, and other subsystems are only referenced where they interact with the reverse proxy/load balancing feature boundary
- **External nginx.org documentation** — The official NGINX documentation at `nginx.org/en/docs/` is out of scope
- **Performance benchmarking** — No runtime testing or performance measurement
- **Security audit** — Security analysis is not part of this deliverable (covered by `SECURITY.md` process)
- **README.md modifications** — The existing README is used as a reference only; no modifications are planned

## 0.9 Execution Parameters

### 0.9.1 Documentation-Specific Instructions

| Parameter | Value |
|-----------|-------|
| Documentation build command | N/A — pure Markdown and standalone HTML; no build step required |
| Documentation preview command | Open `docs/reports/reverse-proxy-archaeology-report.md` in any Markdown renderer with Mermaid support (GitHub, VS Code with Mermaid extension) |
| Diagram generation command | N/A — Mermaid diagrams render client-side; no pre-generation required |
| Presentation preview command | Open `docs/presentations/reverse-proxy-executive-summary.html` in any modern web browser |
| Default format | Markdown with Mermaid diagrams for the report; HTML with reveal.js for the presentation |
| Citation requirement | Every factual claim must cite commit hash, file:line, or "not recorded in-tree" |
| Style guide | Narrative voice targeting engineering leadership; metrics in tables; inferences labeled `[inference]` |
| Documentation validation | Manual review against Directive 1–10 pass/fail criteria; word count ≥2,500; all 12 sections present |

### 0.9.2 Git Analysis Commands Reference

The following git commands are the primary data sources for the archaeology report:

- **Feature file commit history:** `git log --follow --all --format='%h %ai %aN %s' -- <file>`
- **Contributor statistics:** `git log --all --format='%aN' -- <files> | sort | uniq -c | sort -rn`
- **Lines changed per contributor:** `git log --all --author="<name>" --numstat -- <files>`
- **First commit per file:** `git log --follow --diff-filter=A --format='%h %ai' -- <file> | tail -1`
- **Bug-fix commit search:** `git log --oneline --all --grep="fix|bug|segfault|crash|broken|revert" -i -- <files>`
- **TODO/FIXME extraction:** `grep -rn "TODO|FIXME|HACK|XXX|TEMPORARY|WORKAROUND" <files>`
- **TODO age via blame:** `git blame -L <line>,<line> <file> --porcelain`
- **Dormancy gap analysis:** `git log --format='%ai' -- <file>` with date-diff calculation
- **Revert detection:** `git log --oneline --all --grep="revert" -i -- <files>`
- **Integration surface:** `grep -rn "ngx_http_upstream.h" src/ --include="*.c" --include="*.h"`

## 0.10 Rules for Documentation

### 0.10.1 User-Specified Documentation Directives

- **Every factual claim has a citation** — commit hash, file:line, or explicit "not recorded in-tree". Zero exceptions
- **Zero unattributed speculation** — Inferences are labeled as `[inference]`; all other claims must have evidence
- **Metrics in tables, not buried in prose** — All quantitative data presented in Markdown tables
- **Narrative voice** — "The document reads as a narrative, not a data dump — tell the story of how this feature was built"
- **Pass/fail criteria enforced** — Each directive has explicit pass/fail conditions; all must be satisfied
- **Minimum document length** — ≥2,500 words in the final compiled document
- **All 12 sections present** — The compiled narrative must contain all 12 sections from Directive 10
- **Sequential directive execution** — Directives 1–10 executed sequentially; each builds on prior findings

### 0.10.2 Implementation Rule Compliance

- **Observability:** Document observable patterns found in the codebase (logging via `ngx_log_error`/`ngx_log_debug`, defensive error checking patterns in upstream.c: 204 instances, proxy_module.c: 158 instances)
- **Onboarding & Continued Development:** Include suggested next tasks discovered during development in the "Open Questions" section; assess onboarding documentation quality
- **Executive Presentation:** Deliver a reveal.js HTML artifact (`docs/presentations/reverse-proxy-executive-summary.html`) with Mermaid diagrams embedded; every slide has ≥1 visual element
- **Explainability:** Deliver a decision log (`docs/decision-logs/archaeology-report-decisions.md`) as a Markdown table covering all non-trivial methodology decisions
- **Visual Architecture Documentation:** All visual documentation uses Mermaid diagrams with descriptive titles and legends; the upstream state machine evolution shows current state; before/after architecture views included where the feature has changed significantly (e.g., HTTP/2 proxy addition in 2025)

### 0.10.3 Citation Standards

| Citation Type | Format | Example |
|---------------|--------|---------|
| Commit hash | Short hash in monospace | `02025fd6b` |
| File and line | `file:line` format | `src/http/ngx_http_upstream.c:887` |
| Commit with date | Hash + ISO date | `02025fd6b (2005-01-18)` |
| Not recorded | Explicit statement | "rationale not recorded in-tree" |
| Inference | Labeled bracket | `[inference]` |
| Contributor | Full name | Igor Sysoev, Maxim Dounin |

### 0.10.4 Commit Classification Heuristics

Commits are classified using message pattern matching:

| Classification | Pattern | Example |
|----------------|---------|---------|
| `bugfix` | `fix`, `bug`, `regression`, `crash`, `segfault`, `broken`, `revert`, `hotfix`, `patch` | "Upstream: fixed zero size buf alerts on extra data" |
| `feature` | `added`, `support`, `implement`, `introduc`, `new` | "Upstream: early hints support" |
| `refactor` | `refactor`, `style`, `cleanup`, `renamed`, `moved`, `reorgan`, `simplif` | "Proxy: refactored for HTTP/2 support" |
| `config/infra` | Changes to `auto/`, `conf/`, `.github/` | "Module registration updates" |
| `docs` | Changes to `*.md`, `docs/` | "Update GitHub templates and markdown files" |
| `chore` | `Version bump`, `Year 20XX`, style-only changes | "Year 2026" |

## 0.11 References

### 0.11.1 Repository Files and Folders Searched

**Root-level files examined:**
- `README.md` — Project overview, architecture description, Load Balancing getting-started section
- `CONTRIBUTING.md` — Contribution workflow, commit message format (subject prefixes like `Upstream:`, `Proxy:`)
- `SECURITY.md` — Security disclosure policy
- `SUPPORT.md` — Issue/Discussion triage guide
- `CODE_OF_CONDUCT.md` — Contributor Covenant v2.1
- `LICENSE` — BSD-2-Clause license

**Core upstream framework (src/http/):**
- `src/http/ngx_http_upstream.c` — 508 commits, 7,247 LOC; central upstream state machine
- `src/http/ngx_http_upstream.h` — 459 LOC; upstream API contract
- `src/http/ngx_http_upstream_round_robin.c` — 75 commits, 1,042 LOC; default balancer
- `src/http/ngx_http_upstream_round_robin.h` — 239 LOC; round-robin types
- `src/http/ngx_http.c` — HTTP subsystem orchestration (integration context)
- `src/http/ngx_http.h` — HTTP umbrella header
- `src/http/ngx_http_core_module.c` — HTTP core module (location routing context)

**HTTP proxy and load balancer modules (src/http/modules/):**
- `src/http/modules/ngx_http_proxy_module.c` — 283 commits, 5,414 LOC
- `src/http/modules/ngx_http_proxy_module.h` — 127 LOC
- `src/http/modules/ngx_http_proxy_v2_module.c` — 5 commits, 4,160 LOC
- `src/http/modules/ngx_http_upstream_hash_module.c` — 756 LOC
- `src/http/modules/ngx_http_upstream_ip_hash_module.c` — 288 LOC
- `src/http/modules/ngx_http_upstream_least_conn_module.c` — 326 LOC
- `src/http/modules/ngx_http_upstream_random_module.c` — 531 LOC
- `src/http/modules/ngx_http_upstream_keepalive_module.c` — 576 LOC
- `src/http/modules/ngx_http_upstream_zone_module.c` — 1,007 LOC

**Integration surface modules (src/http/modules/):**
- `src/http/modules/ngx_http_fastcgi_module.c` — FastCGI upstream consumer
- `src/http/modules/ngx_http_grpc_module.c` — gRPC upstream consumer
- `src/http/modules/ngx_http_scgi_module.c` — SCGI upstream consumer
- `src/http/modules/ngx_http_uwsgi_module.c` — uWSGI upstream consumer
- `src/http/modules/ngx_http_memcached_module.c` — Memcached upstream consumer

**Stream (L4) proxy and upstream (src/stream/):**
- `src/stream/ngx_stream_proxy_module.c` — 86 commits, 2,683 LOC
- `src/stream/ngx_stream_upstream.c` — 868 LOC
- `src/stream/ngx_stream_upstream.h` — 165 LOC
- `src/stream/ngx_stream_upstream_round_robin.c` — 1,075 LOC
- `src/stream/ngx_stream_upstream_round_robin.h`
- `src/stream/ngx_stream_upstream_hash_module.c`
- `src/stream/ngx_stream_upstream_least_conn_module.c`
- `src/stream/ngx_stream_upstream_random_module.c`
- `src/stream/ngx_stream_upstream_zone_module.c`

**Event layer (src/event/):**
- `src/event/ngx_event_pipe.c` — 92 commits, 1,146 LOC
- `src/event/ngx_event_pipe.h` — Pipe data structures
- `src/event/ngx_event_connect.c` — 435 LOC
- `src/event/ngx_event_connect.h` — Peer connection types

**Configuration and build system:**
- `conf/nginx.conf` — Sample config with proxy_pass scaffold (line 58)
- `conf/fastcgi.conf` — FastCGI parameter definitions
- `auto/options` — Feature flag definitions (HTTP_PROXY, HTTP_UPSTREAM_*, STREAM_UPSTREAM_*)
- `auto/modules` — Module registration and source file linkage

**Documentation directory:**
- `docs/html/index.html` — Default welcome page
- `docs/html/50x.html` — Default error page
- `docs/man/nginx.8` — Man page
- `docs/xml/nginx/changes.xml` — XML changelog
- `docs/xslt/changes.xslt` — XSLT transform
- `docs/xsls/changes.xsls` — XSLS transform
- `docs/dtd/changes.dtd` — Changelog DTD
- `docs/GNUmakefile` — Docs build makefile

**CI/CD configuration:**
- `.github/workflows/buildbot.yml` — CI build workflow
- `.github/workflows/check-pr.yml` — PR check workflow
- `.github/workflows/f5_cla.yml` — CLA enforcement

### 0.11.2 Tech Spec Sections Referenced

- **4.3 REVERSE PROXY WORKFLOW** — Upstream Proxy Request Flow and Load Balancer Decision Flow (Mermaid diagrams and architectural context)
- **4.5 STREAM (L4) PROXY WORKFLOW** — TCP/UDP Session Processing flow (stream proxy architectural context)
- **1.1 EXECUTIVE SUMMARY** — Project overview, NGINX 1.29.5 version, stakeholder groups, business impact

### 0.11.3 Git History Analysis Summary

| Analysis Type | Scope | Results |
|---------------|-------|---------|
| Total repository commits | All branches | 8,518 |
| Feature-related commits (proxy/upstream keyword) | All branches | 792 (9.3%) |
| Unique contributors to feature files | Feature manifest files | 20 total, 11 with ≥3 commits |
| Feature date range | event_pipe.c first → proxy_v2_module.c last | 2003-04-14 to 2025-11-30 |
| Active months | Months with ≥1 feature commit | 221 |
| Bug-fix commits in feature files | grep "fix/bug/crash" in feature file commits | 345 |
| TODO/FIXME entries in HEAD | grep across manifest files | 13 entries |
| Reverts in feature files | grep "revert" in feature file commits | 3 instances |
| Largest file by commits | ngx_http_upstream.c | 508 commits |
| Highest bug-fix ratio | ngx_event_pipe.c | 48% |
| Longest dormancy gap | ngx_http_upstream.c | 521 days (2022-06-22 to 2023-11-25) |

### 0.11.4 Attachments

No external attachments (Figma designs, PDFs, or other files) were provided for this project. All analysis is derived entirely from the in-tree repository content and git history.

