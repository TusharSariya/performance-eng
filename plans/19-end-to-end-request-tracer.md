# Project 19: End-to-End Request Tracer

## Goal

Build a distributed tracing system that follows a single request through a multi-tier web application (reverse proxy → application server → database), using eBPF for kernel-level instrumentation and USDT probes for application-level tracing.

## What You'll Learn

- Distributed tracing concepts (spans, traces, context propagation)
- USDT (User Statically Defined Tracing) probes
- Cross-layer correlation (network ↔ filesystem ↔ application)
- Request latency breakdown (where time is actually spent)
- How production tracing systems (Jaeger, Zipkin, OpenTelemetry) work under the hood

## Background

When a user request is slow, the question is always: **where is the time spent?** Is it the network? The application code? The database? The kernel? Production systems use distributed tracing (OpenTelemetry, Jaeger) but these only instrument application code. They miss:
- Kernel network stack latency
- Disk I/O latency for specific queries
- Context switch and scheduling delays
- TCP retransmissions causing latency

This project combines application-level tracing with kernel-level eBPF instrumentation to get a complete picture.

## Prerequisites

- C with libbpf (for eBPF programs)
- Python/Go/Node (for the sample web application)
- nginx (or similar reverse proxy)
- PostgreSQL or MySQL (database tier)
- Understanding of HTTP and SQL basics

## Milestones

### Milestone 1: Sample Multi-Tier Application

**Build a simple but realistic application to trace.**

- Architecture:
  ```
  Client → nginx (reverse proxy) → App server (Python/Go/Node) → PostgreSQL
  ```
- Application:
  - HTTP API with a few endpoints
  - Each endpoint makes 1-3 database queries
  - Some endpoints do file I/O (read config, write log)
  - Configurable artificial latency for testing
- Add a unique request ID header (`X-Request-ID`) at nginx level
  - Propagate through application to database queries
  - This is the trace/correlation ID

**Deliverable:** Working multi-tier application with request ID propagation.

### Milestone 2: Application-Level Tracing with USDT

**Add static tracepoints to the application.**

- Add USDT probes at key points:
  - `request_start(request_id, method, path)`
  - `request_end(request_id, status_code, duration_ms)`
  - `db_query_start(request_id, query)`
  - `db_query_end(request_id, duration_ms, rows)`
  - `cache_hit(request_id, key)` / `cache_miss(request_id, key)`
- For Python: use `systemtap-sdt-dev` and ctypes, or Python's built-in DTrace probes
- For Go: use `github.com/DataDog/go-usdt` or manual probe placement
- For Node: use `node --enable-dtrace-probes`
- Write eBPF programs that attach to these USDT probes and collect timing data

**Deliverable:** USDT probes in the application + eBPF programs to collect them.

### Milestone 3: Kernel-Level Request Correlation

**Use eBPF to trace the kernel portion of request handling.**

- For each traced request (matched by connection/socket):
  - **Network receive path:**
    - `tcp_recvmsg` → timestamp when request data arrives
    - Time in socket buffer before `recvmsg` → application read latency
  - **Application processing:**
    - Between `recvmsg` return and next `sendmsg` → processing time
  - **Database query:**
    - `tcp_sendmsg` (to DB) → query sent
    - `tcp_recvmsg` (from DB) → response received
    - Delta = database RTT (network + query execution)
  - **File I/O during request:**
    - `vfs_read`/`vfs_write` with stack trace → which files, how long
  - **Network send path:**
    - `tcp_sendmsg` → response sent
    - Time from application write to actual packet TX
- Challenge: correlating socket operations to specific requests requires tracking fd→connection mapping

**Deliverable:** Per-request kernel-level timing breakdown.

### Milestone 4: Trace Assembly and Visualization

**Combine application and kernel traces into a unified view.**

- For each request, assemble a trace with spans:
  ```
  Request abc123: GET /api/users (total: 45.2ms)
  ├─ nginx processing:          2.1ms
  ├─ network: nginx → app:      0.3ms
  ├─ app processing:            42.5ms
  │  ├─ handler setup:          0.5ms
  │  ├─ DB query 1 (SELECT):    15.2ms
  │  │  ├─ query send:          0.1ms
  │  │  ├─ DB processing:       14.8ms
  │  │  └─ result recv:         0.3ms
  │  ├─ business logic:         5.1ms
  │  ├─ DB query 2 (UPDATE):    18.3ms
  │  └─ response serialization: 3.4ms
  ├─ network: app → nginx:      0.2ms
  └─ nginx response:            0.1ms
  ```
- Render as:
  - Text trace (like above)
  - Gantt chart / waterfall diagram (SVG or terminal)
  - JSON compatible with Jaeger/Zipkin format (for visualization in their UIs)

**Deliverable:** Trace assembler with multiple output formats.

### Milestone 5: Anomaly Detection and Comparison

**Automatically detect slow requests and explain why.**

- Collect traces for many requests (1000+)
- Build a baseline:
  - Normal latency distribution for each span type
  - Normal number of DB queries per endpoint
  - Normal I/O pattern per endpoint
- Detect anomalies:
  - "This request's DB query took 200ms (p99 is 50ms)"
  - "This request made 15 DB queries (normal is 3) — N+1 query problem"
  - "45ms was spent in kernel socket buffer (application was busy doing CPU work)"
  - "TCP retransmission added 200ms to database response"
- Compare traces:
  - Side-by-side: normal request vs slow request
  - Highlight which spans differ

**Deliverable:** Anomaly detection and trace comparison tool.

## Key Concepts to Explore

| Concept | Why It Matters |
|---------|---------------|
| Trace context propagation | How to follow a request across process boundaries |
| USDT probes | Zero-overhead when not traced; activated dynamically |
| Span model | Traces are trees of timed spans; standard in OpenTelemetry |
| Head-based vs tail-based sampling | Can't trace every request; need to sample intelligently |
| Kernel-user correlation | Matching kernel events to application-level requests |
| N+1 query detection | Common performance bug detectable through tracing |

## Validation & Benchmarks

- Generate requests with known latency injection at different layers
- Verify total trace time matches actual client-observed latency
- Cross-validate database query times against PostgreSQL's `pg_stat_statements`
- Cross-validate network latency against `ss -ti` RTT values
- Measure tracing overhead: requests/sec with and without tracing
  - Should be <5% overhead for sampled tracing

## References

- Brendan Gregg's "BPF Performance Tools" Chapter 12 (Languages) and Chapter 3 (USDT probes)
- OpenTelemetry specification (trace model)
- Jaeger documentation (trace visualization)
- Google Dapper paper ("Dapper, a Large-Scale Distributed Systems Tracing Infrastructure")
- Linux USDT/DTrace probe documentation
