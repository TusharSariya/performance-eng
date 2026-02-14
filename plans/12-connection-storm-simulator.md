# Project 12: Connection Storm Simulator

## Goal

Build a tool that generates thousands of short-lived TCP connections to stress-test the kernel's connection handling path, using eBPF to measure where time is spent (SYN queue, accept queue, TIME_WAIT recycling).

## What You'll Learn

- TCP connection lifecycle (three-way handshake, data transfer, four-way close)
- SYN queue and accept queue mechanics
- TIME_WAIT state and its impact on connection-heavy workloads
- SYN cookies and SYN flood protection
- `epoll` and connection handling scalability

## Background

Many production systems handle thousands of new connections per second (load balancers, API gateways, microservices). Each connection requires a three-way handshake (1.5 RTT), and closed connections linger in TIME_WAIT for 60 seconds. Under heavy connection churn:
- The SYN queue (half-open connections) can overflow
- The accept queue (completed but unaccepted connections) can fill up
- TIME_WAIT sockets consume memory and ephemeral ports
- The kernel's connection rate becomes the bottleneck

## Prerequisites

- C or Go (for the storm generator)
- C with libbpf (for the eBPF instrumentation)
- Linux with root access
- Two machines or loopback testing

## Milestones

### Milestone 1: Connection Storm Generator

**Build a high-rate TCP connection generator.**

- Client:
  - Open N connections per second to a target
  - Each connection: connect → send small request → receive response → close
  - Use `epoll` for concurrency (not threads-per-connection)
  - Configurable: connections/sec, concurrent connections, request size, hold time
- Server:
  - Accept connections, read request, send response, close
  - Track: connections accepted, active connections, accept latency
- Metrics:
  - Connection rate achieved (connections/sec)
  - Connection establishment latency (SYN → established)
  - Request-response latency
  - Errors (RST, timeouts, connection refused)

**Deliverable:** `./storm --rate 10000 --target 10.0.0.1:8080 --hold 10ms`

### Milestone 2: Queue Monitoring with eBPF

**Instrument the SYN queue and accept queue.**

- eBPF hooks:
  - `tcp_v4_syn_recv_sock` — SYN received, adding to SYN queue
  - `inet_csk_reqsk_queue_add` — moving from SYN queue to accept queue
  - `inet_csk_accept` — application accepts the connection
- Measure:
  - SYN queue depth over time
  - Accept queue depth over time (also readable from `ss -ltn`)
  - Time in SYN queue (SYN received → handshake complete)
  - Time in accept queue (handshake complete → application accept)
  - SYN queue overflows (drops or SYN cookies activated)
  - Accept queue overflows (connection drops)

**Deliverable:** Real-time display of queue depths and latencies during a connection storm.

### Milestone 3: TIME_WAIT Analysis

**Measure and manage TIME_WAIT socket accumulation.**

- During storm, track:
  - TIME_WAIT socket count over time (`ss -s` or `/proc/net/sockstat`)
  - Ephemeral port exhaustion (`/proc/sys/net/ipv4/ip_local_port_range`)
  - Memory consumed by TIME_WAIT sockets
- Test sysctl tuning:
  - `tcp_tw_reuse` — allow reuse of TIME_WAIT sockets for new connections
  - `tcp_fin_timeout` — reduce FIN_WAIT2 timeout (not TIME_WAIT itself)
  - `tcp_max_tw_buckets` — limit total TIME_WAIT sockets
  - Expanding `ip_local_port_range`
- Show: what happens when ephemeral ports are exhausted

**Deliverable:** TIME_WAIT analysis with tuning recommendations.

### Milestone 4: SYN Flood Simulation and Defense

**Simulate a SYN flood and observe kernel defenses.**

- Generate SYN-only connections (connect but never complete handshake)
  - Use raw sockets or `hping3`
- Observe:
  - SYN queue filling up
  - SYN cookies activating (`/proc/sys/net/ipv4/tcp_syncookies`)
  - Legitimate connection impact (latency increase, drops)
- Tune:
  - `tcp_max_syn_backlog` — SYN queue size
  - `tcp_synack_retries` — retransmissions for SYN-ACK
  - `somaxconn` — accept queue size (affects `listen()` backlog)
- Measure: connection rate with and without SYN cookies under flood

**Deliverable:** SYN flood test showing defense mechanisms and their overhead.

### Milestone 5: Connection Scaling Analysis

**Find the maximum connection rate and identify the bottleneck.**

- Incrementally increase connection rate until failures occur
- At each rate, identify the bottleneck:
  - CPU (check `perf top` — is it in the TCP stack?)
  - SYN queue overflow (check `nstat TcpExtListenOverflows`)
  - Accept queue overflow (check `nstat TcpExtListenDrops`)
  - Ephemeral port exhaustion
  - File descriptor limit (`ulimit -n`)
  - Memory (socket buffers)
- Test optimizations:
  - `SO_REUSEPORT` — multiple threads accepting on same port
  - `TCP_FASTOPEN` — skip one RTT for repeat clients
  - `EPOLLEXCLUSIVE` — avoid thundering herd on accept
  - Increasing `somaxconn` and `tcp_max_syn_backlog`
- Find: maximum sustainable connection rate for your hardware

**Deliverable:** Scaling report showing max connection rate and bottleneck at each stage.

## Key Concepts to Explore

| Concept | Why It Matters |
|---------|---------------|
| Three-way handshake cost | Each connection costs 1.5 RTT; adds up at scale |
| SYN queue (request socket) | Half-open connections waiting for ACK; limited resource |
| Accept queue (established backlog) | Completed connections waiting for `accept()`; drops if full |
| TIME_WAIT (2MSL) | 60 seconds of port/memory consumption per closed connection |
| SYN cookies | Stateless SYN flood defense; avoid SYN queue exhaustion |
| SO_REUSEPORT | Kernel load-balances incoming connections across sockets |
| Ephemeral port range | Default 32768-60999; limits outbound connection rate |

## Validation & Benchmarks

- Monitor with `ss -s` during tests (total sockets, TIME_WAIT count)
- Use `nstat -az` for TCP statistics (overflows, drops, retransmits)
- Compare against `wrk` or `ab` connection rates
- Use `perf record -g -a` to profile kernel during connection storms
- Verify with `dmesg` for kernel warnings (SYN flood detected, etc.)

## References

- Brendan Gregg's "Systems Performance" Chapter 10 (Network)
- "BPF Performance Tools" Chapter 10 (Networking)
- Linux TCP backlog explained: `Documentation/networking/ip-sysctl.rst`
- Cloudflare blog: "SYN packet handling in the wild"
- Vincent Bernat: "Scalable TCP SYN backlog"
- `listen(2)` man page (backlog parameter)
