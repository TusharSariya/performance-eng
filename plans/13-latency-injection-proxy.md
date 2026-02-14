# Project 13: Latency Injection Proxy

## Goal

Build a TCP proxy that sits between a client and server, injecting configurable latency, jitter, packet loss, and bandwidth limits — used to test how applications degrade under adverse network conditions.

## What You'll Learn

- TCP proxy architecture (connection splicing, data forwarding)
- Tail latency and its amplification in distributed systems
- Chaos engineering principles
- How applications should (and shouldn't) handle degraded networks
- `epoll`-based event loops and non-blocking I/O

## Background

Most applications work fine on localhost or in a data center with <1ms latency. But in production, things get worse:
- Cross-region calls add 50-200ms
- Network congestion causes jitter
- Packet loss triggers TCP retransmissions (adding 200ms+ per retransmit)
- Tail latency at the 99th percentile can be 10-100x the median

Jeff Dean's "Tail at Scale" paper showed that in a fan-out architecture, even 1% slow responses at each service can cause 50%+ of user requests to be slow. This proxy lets you test those scenarios.

## Prerequisites

- C, Go, or Rust (need efficient async I/O)
- Understanding of TCP sockets and `epoll`/`select`
- A client-server application to test against (anything: Redis, HTTP, gRPC)

## Milestones

### Milestone 1: Basic TCP Proxy

**Build a transparent TCP proxy.**

- Accept incoming connections on a listen port
- For each client connection, establish a connection to the upstream server
- Forward data bidirectionally using `epoll` + non-blocking sockets
- Handle:
  - Multiple concurrent connections
  - Clean connection teardown (FIN from either side)
  - Error handling (RST, connection refused, timeout)
- Zero added latency in this milestone (baseline proxy)

**Deliverable:** `./proxy --listen 0.0.0.0:6380 --upstream 127.0.0.1:6379` that transparently proxies Redis.

### Milestone 2: Latency and Jitter Injection

**Add configurable delay to proxied data.**

- Latency models:
  - **Fixed delay:** add N ms to every packet
  - **Uniform jitter:** delay = base ± random(jitter_range)
  - **Log-normal:** realistic latency distribution (most fast, long tail)
  - **Periodic spikes:** every N seconds, add a large delay for M seconds
- Implementation:
  - Buffer incoming data with a timestamp
  - Use a timer (timerfd or sleep) to delay forwarding
  - Handle bidirectional delays independently (can model asymmetric links)
- Add delay to:
  - First byte only (simulates connection overhead)
  - Every chunk (simulates network latency)
  - Randomly selected connections (simulates partial degradation)

**Deliverable:** `./proxy --latency 50ms --jitter 20ms --upstream host:port`

### Milestone 3: Packet Loss and Error Injection

**Simulate network failures.**

- Loss modes:
  - **Random drop:** drop X% of data chunks (simulates packet loss → TCP retransmit)
  - **Burst loss:** drop N consecutive chunks then pass M (simulates network blips)
  - **Connection reset:** randomly RST X% of connections
  - **Connection timeout:** stop forwarding (simulate hung backend)
  - **Slow drain:** throttle bandwidth to X KB/s (simulate degraded link)
- For TCP, "packet loss" means:
  - Close the proxy→server connection (causes retransmit or RST to client)
  - Or buffer and delay (simulates retransmission delay)
- Configurable per-connection vs global

**Deliverable:** `./proxy --loss 1% --bandwidth 100KB/s --reset-rate 0.1%`

### Milestone 4: Protocol-Aware Injection

**Add intelligence about the protocol being proxied.**

- HTTP awareness:
  - Parse HTTP request/response boundaries
  - Inject delay after request headers (simulates slow backend processing)
  - Return 503 for X% of requests
  - Slow-drip response body (simulates slow backend)
- Redis awareness:
  - Parse RESP protocol
  - Delay specific commands (e.g., slow down GET but not SET)
  - Return errors for specific commands
- Generic pattern matching:
  - Delay data that matches a regex
  - Useful for testing specific query patterns

**Deliverable:** `./proxy --protocol http --slow-response 5% --delay-body 100ms`

### Milestone 5: Dashboard and Chaos Scenarios

**Build a control plane for the proxy.**

- HTTP API for runtime control:
  - `POST /config` — update latency/loss parameters without restart
  - `GET /stats` — current connection count, bytes forwarded, latencies applied
  - `POST /scenario` — apply a predefined chaos scenario
- Predefined scenarios:
  - "Network partition" — drop all traffic for N seconds
  - "Datacenter failover" — gradually increase latency then drop
  - "Black Friday" — simulate increasing load with degrading response times
  - "GC pause" — periodic 200ms freezes
- Real-time metrics:
  - Proxied request latency histogram (with and without injected delay)
  - Error rate over time
  - Connection pool status

**Deliverable:** Proxy with HTTP control API and predefined chaos scenarios.

## Key Concepts to Explore

| Concept | Why It Matters |
|---------|---------------|
| Tail latency amplification | p99 latency of fan-out = much worse than individual p99 |
| Timeout tuning | Too short = false failures; too long = cascading slowdowns |
| Circuit breakers | Apps should stop calling failing services; proxy tests this |
| Retry storms | Retries on slow services make things worse; proxy reveals this |
| Backpressure | How does the app handle a full send buffer? |
| Graceful degradation | Does the app return partial results or fail entirely? |

## Validation & Benchmarks

- Measure proxy overhead with zero injection (should be <0.1ms added latency)
- Verify injected latency matches configuration (histogram of actual delays)
- Test with Redis: `redis-benchmark` through proxy vs direct
- Test with HTTP: `wrk` through proxy vs direct
- Verify connection handling: open 10K connections through proxy, ensure no leaks

## References

- Jeff Dean: "Tail at Scale" (Google)
- Netflix Chaos Engineering principles
- `toxiproxy` by Shopify (inspiration for this project)
- `tc netem` for kernel-level network simulation
- Brendan Gregg's "Systems Performance" Chapter 10 (Network)
