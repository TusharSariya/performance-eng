# Project 17: Security Event Monitor

## Goal

Build an eBPF-based security monitoring tool that detects suspicious behavior — unexpected process execution, privilege escalation, sensitive file access — a mini version of Falco.

## What You'll Learn

- eBPF for security monitoring (LSM hooks, tracepoints, kprobes)
- Linux security model (capabilities, namespaces, seccomp)
- Detection engineering (writing rules for suspicious behavior)
- Process execution monitoring (`execve` tracing)
- File integrity monitoring concepts

## Background

Traditional security tools (auditd, OSSEC) have significant overhead and delayed detection. eBPF enables real-time, low-overhead security monitoring by running detection logic inside the kernel. Tools like Falco (Sysdig), Tetragon (Cilium), and Tracee (Aqua) use eBPF for production security monitoring.

This project builds a simplified version that teaches the core concepts: what to monitor, how to hook it, and how to detect anomalies.

## Prerequisites

- C with libbpf (CO-RE)
- Linux kernel 5.x+ with BTF
- Root access
- Understanding of Linux process model and file permissions

## Milestones

### Milestone 1: Process Execution Monitor

**Track every process execution on the system.**

- Hook `tracepoint:syscalls:sys_enter_execve` (or `sched:sched_process_exec`)
- For each execution, capture:
  - Timestamp
  - PID, PPID, UID, GID
  - Command name and full arguments
  - Executable path
  - Current working directory
  - Parent process name (build process tree context)
- Store a process tree in userspace (track parent-child relationships)
- Alert on:
  - Execution from unusual directories (`/tmp`, `/dev/shm`, `/var/tmp`)
  - Shell spawned by a web server (e.g., `nginx` → `sh`)
  - Execution by `root` that wasn't expected
  - Binary not in an allow-list

**Deliverable:** `./secmon --watch-exec` logging all process executions with alerts.

### Milestone 2: File Access Monitor

**Detect access to sensitive files.**

- Hook `tracepoint:syscalls:sys_enter_openat` (or kprobe on `vfs_open`)
- Monitor access to:
  - `/etc/shadow`, `/etc/passwd` — credential files
  - `/etc/sudoers` — privilege configuration
  - SSH keys (`~/.ssh/`)
  - Kubernetes secrets (`/var/run/secrets/`)
  - Custom paths (configurable watchlist)
- For each access, capture:
  - Process name, PID, UID
  - File path
  - Access flags (read, write, create, truncate)
  - Full process ancestry (who spawned this process?)
- Alert levels:
  - INFO: expected access (e.g., `sshd` reading `/etc/shadow`)
  - WARN: unusual access (e.g., `python` reading `/etc/shadow`)
  - CRITICAL: write to sensitive files

**Deliverable:** File access monitor with configurable watchlist and alert levels.

### Milestone 3: Privilege Escalation Detection

**Detect processes gaining elevated privileges.**

- Monitor:
  - `setuid`/`setgid` syscalls — process changing its UID/GID
  - `capset` — process gaining capabilities
  - SUID binary execution — `execve` of a file with setuid bit
  - Namespace operations — `unshare`, `setns` (container escapes)
  - `ptrace` calls — process debugging/injecting into another process
- Hook:
  - `tracepoint:syscalls:sys_enter_setuid`
  - `tracepoint:syscalls:sys_enter_setgid`
  - kprobe on `cap_capable` (check capability usage)
  - kprobe on `commit_creds` (credential changes)
- Alert on:
  - UID change from non-root to root (privilege escalation)
  - Unexpected capability usage
  - `ptrace` by non-debugger processes
  - `unshare(CLONE_NEWUSER)` (user namespace creation)

**Deliverable:** Privilege escalation detector with real-time alerts.

### Milestone 4: Network Connection Monitor

**Track network connections for anomaly detection.**

- Hook:
  - `tracepoint:sock:inet_sock_set_state` — TCP state changes
  - kprobe on `tcp_v4_connect` — outbound connections
  - kprobe on `inet_csk_accept` — inbound connections
- For each connection:
  - Source/destination IP and port
  - Process name and PID
  - Connection direction (inbound vs outbound)
  - Protocol (TCP/UDP)
- Alert on:
  - Reverse shell patterns (shell process with network connection)
  - Connections to known-bad ports (4444, 1337, etc.)
  - Unexpected outbound connections from server processes
  - DNS exfiltration (many unique DNS queries)
  - Connections from processes that shouldn't use the network

**Deliverable:** Network connection monitor with behavioral alerts.

### Milestone 5: Rule Engine and Alert Pipeline

**Build a configurable rule engine for detection logic.**

- Rule format (YAML):
  ```yaml
  - name: shell_from_webserver
    description: "Web server spawned a shell"
    condition:
      event: exec
      parent_comm: ["nginx", "apache2", "node"]
      comm: ["sh", "bash", "dash", "zsh"]
    severity: critical

  - name: sensitive_file_write
    description: "Write to sensitive file"
    condition:
      event: open
      path_prefix: ["/etc/shadow", "/etc/sudoers"]
      flags_include: O_WRONLY
    severity: critical
  ```
- Alert outputs:
  - Stdout (formatted log lines)
  - JSON (for log aggregation)
  - Syslog
  - Webhook (HTTP POST for Slack/PagerDuty)
- Event enrichment:
  - Container ID (from cgroup path)
  - Kubernetes pod/namespace (from cgroup or environment)
  - Process tree context (full ancestry)

**Deliverable:** Rule engine with YAML configuration and multiple alert outputs.

## Key Concepts to Explore

| Concept | Why It Matters |
|---------|---------------|
| eBPF LSM hooks | BPF_PROG_TYPE_LSM allows enforcing policy, not just detecting |
| Process lineage | Context matters: `bash` from `sshd` is normal; `bash` from `nginx` is not |
| Container awareness | Must distinguish host processes from container processes |
| False positive rate | Too many alerts = alert fatigue = ignored alerts |
| TOCTOU races | Time-of-check vs time-of-use gaps in security monitoring |
| Performance overhead | Security monitoring must not degrade application performance |

## Validation & Benchmarks

- Test each detection with deliberate trigger:
  - Run `python -c "open('/etc/shadow')"` → should alert
  - Spawn shell from a web server process → should alert
  - Run `sudo su` → should detect privilege escalation
- Measure overhead: run application benchmarks with and without the monitor
- False positive test: run normal system operations for 24 hours, review alerts
- Compare detection coverage against Falco's default rule set

## References

- Falco rule set: https://github.com/falcosecurity/rules
- Tetragon (Cilium) documentation
- Tracee (Aqua Security) documentation
- "BPF Performance Tools" Chapter 11 (Security)
- MITRE ATT&CK framework (for detection categories)
- Brendan Gregg's "Systems Performance" Chapter 11 (Security observability)
