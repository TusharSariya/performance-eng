# Project 20: Performance Regression CI Pipeline

## Goal

Build a system that runs benchmarks on every commit, detects performance regressions statistically (not just "is it slower"), stores historical data, and alerts on significant degradation.

## What You'll Learn

- Statistical methods for performance comparison (not just "compare averages")
- Benchmark design and noise reduction
- CI/CD pipeline integration for performance testing
- Time-series storage for benchmark history
- Change-point detection algorithms

## Background

Performance regressions are among the hardest bugs to catch. They often:
- Don't cause test failures (functionally correct but slow)
- Are too small to notice in development (5% regression)
- Accumulate over time (death by a thousand cuts)
- Are only caught by users in production

Companies like Google, Netflix, and Meta run continuous benchmarks to catch regressions early. The hard part isn't running benchmarks — it's determining whether a change is **statistically significant** given the inherent noise in performance measurements.

## Prerequisites

- Any language (for the benchmark framework and pipeline)
- Git (for commit tracking)
- SQLite or PostgreSQL (for historical data)
- Python (for statistical analysis) or implement in your language of choice
- CI system (GitHub Actions, or just a cron job)

## Milestones

### Milestone 1: Benchmark Framework

**Build a benchmark runner that produces reliable, comparable results.**

- Benchmark specification format:
  ```yaml
  benchmarks:
    - name: json_parse_large
      command: ./bench_json --input large.json --iterations 1000
      metrics: [throughput_ops, latency_p50_us, latency_p99_us, memory_peak_kb]
      warmup_iterations: 100

    - name: http_requests
      command: wrk -t4 -c100 -d30s http://localhost:8080/api
      metrics: [requests_sec, latency_avg_ms, latency_p99_ms]
      setup: ./start_server.sh
      teardown: ./stop_server.sh
  ```
- Before each benchmark run:
  - Pin CPU frequency (`cpupower frequency-set -g performance`)
  - Drop filesystem caches
  - Kill unnecessary background processes
  - Record system state (CPU model, memory, kernel version, load average)
- Run each benchmark N times (configurable, default 10)
- Record: all individual measurements, mean, median, stddev, min, max, p95, p99
- Output: JSON with all raw data and summary statistics

**Deliverable:** Benchmark runner that produces stable, reproducible measurements.

### Milestone 2: Statistical Regression Detection

**Determine if a change caused a statistically significant regression.**

- Problem: benchmarks are noisy. A 2% difference might be noise or a real regression.
- Statistical tests to implement:
  - **Welch's t-test:** compare means of two samples (before/after commit)
    - Account for unequal variance
    - Use p-value threshold (e.g., p < 0.01)
  - **Mann-Whitney U test:** non-parametric alternative (doesn't assume normal distribution)
  - **Effect size (Cohen's d):** how large is the difference in standard deviation units?
    - Small: d < 0.2 (probably don't care even if statistically significant)
    - Medium: 0.2 < d < 0.8
    - Large: d > 0.8 (definitely care)
  - **Confidence intervals:** report "throughput changed by -3.2% ± 1.1% (95% CI)"
- Decision logic:
  - Regression if: p < 0.01 AND effect size > threshold AND direction is worse
  - Improvement if: p < 0.01 AND effect size > threshold AND direction is better
  - No change if: p >= 0.01 OR effect size < threshold

**Deliverable:** Statistical comparison module that determines "regression/improvement/no change" with confidence.

### Milestone 3: Historical Database and Trend Analysis

**Store benchmark results over time and detect trends.**

- Database schema:
  ```sql
  benchmarks(id, name, commit_sha, commit_date, branch, runner_id)
  measurements(id, benchmark_id, metric_name, iteration, value)
  comparisons(id, benchmark_id, base_commit, head_commit, p_value, effect_size, verdict)
  system_info(id, benchmark_id, cpu_model, kernel, memory_gb)
  ```
- Store every measurement (not just summaries) for re-analysis
- Trend analysis:
  - Plot metric over time (last 100 commits)
  - Detect gradual degradation (rolling window comparison)
  - Change-point detection: find the exact commit where performance shifted
    - PELT algorithm (Pruned Exact Linear Time)
    - Or simpler: sliding window t-test
- Identify:
  - "Throughput has decreased 15% over the last 50 commits"
  - "Change point detected at commit abc123 (merged 2024-01-15)"

**Deliverable:** Historical database with trend analysis and change-point detection.

### Milestone 4: CI Integration

**Run benchmarks automatically on every PR/commit.**

- GitHub Actions workflow (or equivalent):
  1. On PR: run benchmarks against PR branch and base branch
  2. Compare results using statistical module
  3. Post PR comment with results:
     ```
     ## Performance Report

     | Benchmark        | Base     | PR       | Change   | Status |
     |------------------|----------|----------|----------|--------|
     | json_parse_large | 12,340/s | 12,102/s | -1.9%    | ✅ OK  |
     | http_requests    | 8,501/s  | 7,203/s  | -15.3%   | ❌ REG |
     | memory_peak      | 42 MB    | 44 MB    | +4.8%    | ⚠️ WARN |

     ❌ **Regression detected in http_requests**
     p-value: 0.0003, effect size: 1.2 (large)
     Confidence interval: -15.3% ± 2.1%
     ```
  4. Block merge if regression exceeds threshold (configurable)
- Handle flaky benchmarks:
  - If initial comparison is inconclusive, run more iterations
  - Track flakiness rate per benchmark (how often does it give conflicting results?)
  - Quarantine consistently noisy benchmarks

**Deliverable:** CI pipeline that runs benchmarks, posts results, and blocks on regressions.

### Milestone 5: Dashboard and Alerting

**Long-term visibility into performance trends.**

- Web dashboard (or Grafana) showing:
  - Time-series of each benchmark metric (with confidence bands)
  - Change-point annotations (mark commits that caused shifts)
  - Regression history (timeline of detected regressions and resolutions)
  - Benchmark health (which benchmarks are noisy, which are stable)
  - Comparison view: select two commits, see all benchmark differences
- Alerting:
  - Slack/email notification on regression detection
  - Weekly performance summary (any trends to watch?)
  - Alert on benchmark infrastructure issues (runner slowdown, disk full)
- Performance budget:
  - Define targets per benchmark (e.g., "json_parse must be > 10,000 ops/s")
  - Alert when approaching budget limit (within 10%)

**Deliverable:** Performance dashboard with alerting and budget tracking.

## Key Concepts to Explore

| Concept | Why It Matters |
|---------|---------------|
| Statistical significance vs practical significance | A 0.1% change can be statistically significant with enough samples, but irrelevant |
| Benchmark noise sources | CPU throttling, background processes, ASLR, filesystem caches |
| Warmup | JIT compilation, cache filling, branch predictor training |
| Wall clock vs CPU time | Wall clock includes I/O and scheduling; CPU time is deterministic |
| Change-point detection | More powerful than pairwise comparison for finding gradual regressions |
| Benchmark game | Optimizing for benchmarks vs real-world performance |

## Validation & Benchmarks

- Introduce a known regression (add a `sleep(1ms)` to a hot path) and verify detection
- Introduce a known improvement and verify detection
- Make no change and verify no false positive over 20 runs
- Test with noisy benchmarks (I/O-bound) and verify the system handles noise
- Validate statistical claims: when the system says "p < 0.01", the false positive rate should actually be <1%

## References

- Google's "Continuous Profiling" and benchmark infrastructure
- "Rigorous Benchmarking in Reasonable Time" (Kalibera, Jones)
- Netflix Performance Engineering blog posts
- `criterion.rs` and `google/benchmark` (inspiration for the benchmark framework)
- Change-point detection: PELT algorithm (Killick et al.)
- GitHub Actions documentation
