# Core Model Configuration Guide

This document describes how to configure the `core` CPU model
(`models/cpu/src/core`). Every configurable knob is a
[Sparta](https://github.com/sparcians/map) parameter attached to a unit in the
simulation tree, so the model is configured with standard Sparta YAML files and
command-line overrides.

## Table of Contents

1. [Supplying Parameters](#supplying-parameters)
2. [Simulation Tree](#simulation-tree)
3. [Core](#core)
4. [Frontend](#frontend)
   - [Fetch](#fetch)
   - [I-Cache](#i-cache)
   - [Fetch Queue and Decode Queue](#fetch-queue-and-decode-queue)
   - [Decode](#decode)
   - [Branch Predictor](#branch-predictor)
5. [Midcore](#midcore)
   - [Rename](#rename)
   - [Issue](#issue)
   - [Execute](#execute)
   - [Load Store Queue (LSQ)](#load-store-queue-lsq)
   - [D-Cache](#d-cache)
   - [Writeback / ROB](#writeback--rob)
6. [L2 Cache](#l2-cache)
7. [Advanced Microarchitectural Features](#advanced-microarchitectural-features)
   - [Write-Port Arbiter](#write-port-arbiter)
   - [Writeback Buffer](#writeback-buffer)
   - [Bypass Network and Speculative Wakeup](#bypass-network-and-speculative-wakeup)
   - [Register File Banking](#register-file-banking)
8. [Execution Driver](#execution-driver)
9. [Visualization and Debugging](#visualization-and-debugging)
10. [Example Configurations](#example-configurations)

---

## Supplying Parameters

Parameters live at `top.core0.<unit>.params.<name>` (core-wide parameters at
`top.core0.params.<name>`). They can be set in two ways:

- **YAML config files** with `-c FILE.yaml`. `-c` may be repeated; later files
  override earlier ones, which makes small override files convenient:
  ```bash
  ./build/core/core -i 0 -c models/cpu/src/core/config.yaml -c my_overrides.yaml \
      --target-elf tests/build/coremark.bare.elf
  ```
- **Single overrides** with `-p PATH VALUE`:
  ```bash
  -p top.core0.writeback.params.retire_width 8
  -p top.core0.issue.params.bypass_paths "[alu:any:0,load:any:1]"
  ```

The model does not read any config file implicitly; pass `-c` to use one. When
a parameter is not set, the built-in default listed in the tables below applies.
Note that the shipped `config.yaml` overrides several defaults (for example it
selects structural caches and the `typed` execute granularity).

A YAML file mirrors the tree, with parameters under a `params` key:

```yaml
top.core0:
  params:
    ooo_enabled: true
  fetch:
    params:
      fetch_width: 16
  writeback:
    params:
      retire_width: 4
      rob_capacity: 128
```

Useful Sparta options for inspecting the configuration:

| Option | Description |
|--------|-------------|
| `--help-parameters` | Print every parameter with its current and default value, then exit |
| `--show-parameters` | Print the parameter tree after configuration and continue running |
| `--write-final-config FILE` | Write the fully resolved configuration as YAML |
| `--show-tree` | Print the unit tree |

Run-control flags (`-i`, `-r`, `--target-elf`, `--cpu-freq`, `--report-all`)
are described in the top-level [README](../../../../README.md).

---

## Simulation Tree

```
top
└── core0                 (params: core-wide settings)
    ├── fetch             FetchStructures
    ├── icache            FrontendMemoryStructures
    ├── fetch_queue       FetchQueue
    ├── decode            DecodeStructures
    ├── decode_queue      DecodeQueue
    ├── branch_predictor  BranchPredictor
    ├── rename            Rename
    ├── issue             Issue
    ├── execute           Execute
    ├── lsq               LSQ
    ├── dcache            BackendMemoryStructures
    ├── writeback         Writeback (ROB)
    ├── l2cache           L2Cache
    ├── flush_arbiter     FlushArbiter
    ├── pipeline_clock    PipelineClock (no parameters)
    └── edriver           ExecutionDriver (created when --target-elf or --trace-file is given)
```

---

## Core

`top.core0.params`

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `ooo_enabled` | bool | `true` | Out-of-order pipeline (`true`) or in-order pipeline (`false`) |
| `wrong_path_enabled` | bool | `false` | Fetch and execute down mispredicted paths |
| `wrong_path_max_depth` | uint32 | `8` | Maximum number of nested speculation levels |
| `misprediction_penalty_cycles` | uint32 | `15` | Fetch stall applied when a misprediction redirect reaches Fetch while `wrong_path_enabled` is `false` (see below) |
| `bypass_queues` | bool | `true` | Skip the FetchQueue and DecodeQueue (see below) |
| `visualizer_*` | | | Pipeline visualizer, see [Visualization and Debugging](#visualization-and-debugging) |
| `cache_viewer_*` | | | Cache viewer, see [Visualization and Debugging](#visualization-and-debugging) |

**In-order vs. out-of-order.** `ooo_enabled` selects how the pipeline is wired:
Rename → Issue → Execute in out-of-order mode, Rename → Execute (with an
architectural scoreboard in Rename) in in-order mode. Rename keeps its own copy
of the setting (`rename.params.ooo_enabled`) to choose its dispatch algorithm,
so set both to the same value. `config_inorder.yaml` does exactly that.

**Wrong-path speculation.** With `wrong_path_enabled: true`, a mispredicted
branch steers Fetch down the predicted (wrong) path, Rename checkpoints the RAT
for every branch, and Execute flushes and redirects the pipeline when the branch
resolves. With it disabled, wrong-path instructions are never fetched; the cost
of a misprediction is modeled by the Decode stall `decode.params.bp_mispred_penalty`.
Execute only raises branch-misprediction redirects when speculation is enabled,
so `misprediction_penalty_cycles` is not applied to branch mispredictions.

**Queue bypass.** With `bypass_queues: true`, I-cache output goes straight to
Decode and Decode output straight to Rename; the FetchQueue and DecodeQueue are
left unconnected. The shipped `config.yaml` sets it to `false`.

---

## Frontend

### Fetch

`top.core0.fetch.params`

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `initial_pc` | uint64 | `0x80000000` | Initial PC. Overridden by the ELF entry point when an ELF is loaded |
| `fetch_width` | uint32 | `4` | Bytes per fetch group. A group also ends at a backward control transfer or a serializing instruction |
| `fetch_buffer_capacity` | uint32 | `64` | Fetch buffer size in bytes (currently not used by the model) |

### I-Cache

`top.core0.icache.params`

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `cache_mode` | string | `probabilistic` | `probabilistic` or `structural` |
| `hit_latency` | uint32 | `1` | Hit latency in cycles |
| `miss_latency` | uint32 | `20` | Additional miss latency when no L2 is configured |
| `mshr_capacity` | uint32 | `4` | Maximum outstanding misses |
| `hit_rate` | double | `1.0` | Hit probability (probabilistic mode) |
| `rng_seed` | uint64 | `0xCAFE` | Seed of the hit/miss RNG (probabilistic mode) |
| `cache_size_kb` | uint64 | `64` | Capacity in KB (structural mode) |
| `cache_line_size` | uint64 | `64` | Line size in bytes (structural mode) |
| `cache_associativity` | uint64 | `8` | Ways per set (structural mode) |
| `replacement_policy` | string | `lru` | `lru` (true LRU) or `plru` (tree pseudo-LRU) (structural mode) |

**Cache modes.** The same two modes are available for the I-cache and the
D-cache:

- `probabilistic` draws a hit or miss for every request from `hit_rate`. Misses
  take `hit_latency + miss_latency` cycles. No tag state is kept.
- `structural` models a tag array with sets, ways and the selected replacement
  policy, MSHRs with miss coalescing, and hit-under-miss. Misses are filled from
  the L2 when it is enabled, otherwise after `hit_latency + miss_latency`
  cycles. Only structural caches can be connected to the L2 and traced by the
  cache viewer.

### Fetch Queue and Decode Queue

`top.core0.fetch_queue.params`, `top.core0.decode_queue.params`

| Parameter | Type | Default (FQ / DQ) | Description |
|-----------|------|-------------------|-------------|
| `capacity` | uint32 | `16` / `32` | Queue entries (instructions / uops) |
| `log_enabled` | bool | `false` | Currently has no effect; use Sparta logging instead |

Both queues are only used when `bypass_queues` is `false`. A branch at the head
of the FetchQueue waits until its branch prediction has arrived.

### Decode

`top.core0.decode.params`

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `lat_alu` | uint32 | `1` | Execution latency of ALU uops |
| `lat_mul` | uint32 | `3` | Execution latency of integer multiplies |
| `lat_div` | uint32 | `12` | Execution latency of integer divides |
| `lat_branch` | uint32 | `1` | Execution latency of branches and jumps |
| `lat_load` | uint32 | `1` | Load address-generation latency (cache latency is added by the D-cache) |
| `lat_store` | uint32 | `1` | Store address-generation latency |
| `lat_fp` | uint32 | `4` | Execution latency of floating-point uops |
| `lat_vec` | uint32 | `4` | Execution latency of vector uops |
| `lat_fence` | uint32 | `1` | Execution latency of fence uops |
| `bp_mispred_penalty` | uint32 | `5` | Decode stall cycles after a mispredicted branch |

Decode classifies every instruction into a uop type, which selects its latency,
its issue scheduler and its execute group:

| Uop type | Instructions |
|----------|--------------|
| `ALU` | All instructions not listed below |
| `Mul` | Mnemonics starting with `mul` |
| `Div` | Mnemonics starting with `div` or `rem` |
| `Branch` | Conditional branches, jumps, calls and returns |
| `Load` | Scalar loads |
| `Store` | Scalar stores and atomics |
| `FpOp` | Instructions the execution driver reports as floating point |
| `VecOp` | Instructions the execution driver reports as vector |
| `Fence` | Reserved; no instruction is currently classified as a fence |

The execution driver currently reports the vector type only for vector loads
and stores, and never reports the floating-point type, so floating-point and
vector arithmetic instructions are classified as `ALU` uops.

### Branch Predictor

`top.core0.branch_predictor.params`

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `accuracy` | double | `0.95` | Probability that a branch is predicted correctly |
| `rng_seed` | uint64 | `0xC0FFEE` | Seed of the prediction RNG; the same seed gives the same sequence of mispredictions |

The predictor is probabilistic: Fetch sends every branch and jump to it, and
the predictor flips the actual outcome with probability `1 - accuracy`.

---

## Midcore

### Rename

`top.core0.rename.params`

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `dispatch_width` | uint32 | `4` | Instructions renamed and dispatched per cycle |
| `num_phys_regs` | uint32 | `128` | Physical registers per register file (integer, FP and vector each) |
| `freelist_headroom` | uint16 | `8` | Dispatch stalls unless at least this many free registers remain after allocation |
| `ooo_enabled` | bool | `true` | Out-of-order dispatch; keep equal to `top.core0.params.ooo_enabled` |
| `max_branch_checkpoints` | uint32 | `32` | RAT checkpoints available for in-flight branches (used with wrong-path speculation) |
| `regfile_banking_enabled` | bool | `false` | See [Register File Banking](#register-file-banking) |
| `regfile_num_banks` | uint32 | `4` | See [Register File Banking](#register-file-banking) |
| `regfile_reads_per_bank` | uint32 | `2` | See [Register File Banking](#register-file-banking) |
| `regfile_writes_per_bank` | uint32 | `1` | See [Register File Banking](#register-file-banking) |
| `log_enabled` | bool | `false` | Currently has no effect; use Sparta logging instead |

Dispatch also stalls when the ROB is full, when the target issue scheduler (or,
in in-order mode, execute group) is full, and when no load- or store-queue entry
is available for a memory uop: the LSQ grants entries as credits at rename, in
program order. In in-order mode it additionally waits until the source operands
are ready.

### Issue

`top.core0.issue.params`

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `mode` | string | `unified` | `unified` (one scheduler) or `partitioned` (several schedulers) |
| `issue_queue_capacity` | uint32 | `16` | Scheduler entries (unified mode, at most 64) |
| `issue_width` | uint32 | `4` | Issue ports. In partitioned mode they are split evenly between the schedulers (at least one each) |
| `selection` | string | `age_matrix` | Selection among ready entries: `age_matrix` (oldest first), `fifo` (lowest slot first) or `random` |
| `num_phys_regs` | uint32 | `128` | Scoreboard size; keep equal to `rename.params.num_phys_regs` |
| `speculative_wakeup` | bool | `false` | See [Bypass Network and Speculative Wakeup](#bypass-network-and-speculative-wakeup) |
| `bypass_network_enabled` | bool | `false` | See [Bypass Network and Speculative Wakeup](#bypass-network-and-speculative-wakeup) |
| `bypass_regfile_read_latency` | uint32 | `1` | See [Bypass Network and Speculative Wakeup](#bypass-network-and-speculative-wakeup) |
| `bypass_paths` | string list | `[]` | See [Bypass Network and Speculative Wakeup](#bypass-network-and-speculative-wakeup) |
| `scheduler_configs` | string list | `[]` | Schedulers for partitioned mode, one `name:capacity:types` entry each |
| `routing_priorities` | string list | `[]` | Routing policy chain for partitioned mode |
| `routing_dep_occupancy_limit` | double | `0.9` | Dependency routing is skipped when the producer's scheduler is fuller than this |
| `routing_overflow_threshold` | double | `0.8` | Reserved; currently not used |

**Partitioned mode.** Each `scheduler_configs` entry has the form
`name:capacity:types`, where `types` is a `+`-separated list of uop types
(`ALU`, `Mul`, `Div`, `Branch`, `Load`, `Store`, `FpOp`, `VecOp`, `Fence`). Every
uop type must be assigned to at least one scheduler; the first scheduler that
lists a type is that type's primary scheduler.

```yaml
top.core0:
  issue:
    params:
      mode: partitioned
      issue_width: 6
      scheduler_configs: ["int:16:ALU+Mul+Div+Branch+Fence", "mem:16:Load+Store", "fp:8:FpOp+VecOp"]
      routing_priorities: ["dependency_locality", "type_affinity", "least_occupied"]
```

`routing_priorities` is an ordered list of policies; the first one that finds a
non-full scheduler wins, and the type's primary scheduler is the fallback:

| Policy | Description |
|--------|-------------|
| `dependency_locality` | The scheduler that received the producer of one of the instruction's source operands, if it accepts the uop type and is not fuller than `routing_dep_occupancy_limit` |
| `type_affinity` | The primary scheduler of the uop type |
| `least_occupied` | The least occupied scheduler that accepts the uop type |
| `round_robin` | Rotate between the schedulers that accept the uop type |

When `routing_priorities` is empty, the chain is `dependency_locality`,
`type_affinity`, `least_occupied`, and `routing_dep_occupancy_limit` keeps its
built-in value of 0.9.

### Execute

`top.core0.execute.params`

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `granularity` | string | `unified` | Functional-unit grouping: `unified`, `typed` or `functional` |
| `unified_max_in_flight` | uint32 | `16` | In-flight uops (unified) |
| `int_max_in_flight` | uint32 | `4` | In-flight uops in the integer group (typed) |
| `fp_max_in_flight` | uint32 | `2` | In-flight uops in the FP group (typed) |
| `vec_max_in_flight` | uint32 | `2` | In-flight uops in the vector group (typed) |
| `branch_max_in_flight` | uint32 | `2` | In-flight uops in the branch group (typed) |
| `alu_count`, `alu_per_unit_in_flight` | uint32 | `2`, `4` | ALU units and in-flight uops per unit (functional) |
| `mul_count`, `mul_per_unit_in_flight` | uint32 | `1`, `2` | Multiply units (functional) |
| `div_count`, `div_per_unit_in_flight` | uint32 | `1`, `1` | Divide units (functional) |
| `branch_count`, `branch_per_unit_in_flight` | uint32 | `1`, `2` | Branch units (functional) |
| `fp_count`, `fp_per_unit_in_flight` | uint32 | `1`, `4` | FP units (functional) |
| `vec_count`, `vec_per_unit_in_flight` | uint32 | `1`, `4` | Vector units (functional) |
| `fence_count`, `fence_per_unit_in_flight` | uint32 | `1`, `1` | Fence units (functional) |
| `write_port_mode`, `write_port_count`, `write_port_mapping` | | | See [Write-Port Arbiter](#write-port-arbiter) |
| `writeback_buffer_*` | | | See [Writeback Buffer](#writeback-buffer) |

The granularity determines the execute groups. A group accepts new uops while
its in-flight count is below its limit; the group index is also the source id
used by write-port mappings:

| Granularity | Groups (index: uop types) |
|-------------|---------------------------|
| `unified` | 0: all uop types |
| `typed` | 0: ALU, Mul, Div, Load, Store, Fence; 1: FpOp; 2: VecOp; 3: Branch |
| `functional` | 0: ALU; 1: Mul; 2: Div; 3: Branch; 4: Load, Store; 5: FpOp; 6: VecOp; 7: Fence |

In `functional` mode the limit of each group is `<unit>_count ×
<unit>_per_unit_in_flight`; the load/store group (4) uses the ALU values.
Memory uops leave Execute after address generation and complete in the LSQ.

### Load Store Queue (LSQ)

`top.core0.lsq.params`

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `load_queue_capacity` | uint32 | `32` | Load queue entries |
| `store_queue_capacity` | uint32 | `24` | Store queue entries |
| `store_forwarding` | bool | `true` | Let a load complete from an older store in the store queue that fully covers it |
| `forwarding_latency` | uint32 | `1` | Cycles for a forwarded load to complete |
| `num_banks` | uint32 | `1` | Load/store queue banks (power of two). Capacities are divided between banks; values above 1 are experimental |
| `cache_line_size` | uint32 | `64` | Line size used to select the bank of an address |

Stores complete towards the ROB as soon as they enter the store queue; they are
written to the D-cache after they retire.

### D-Cache

`top.core0.dcache.params`

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `cache_mode` | string | `probabilistic` | `probabilistic` or `structural`, see [Cache modes](#i-cache) |
| `hit_latency` | uint32 | `1` | Hit latency in cycles |
| `miss_latency` | uint32 | `10` | Additional miss latency when no L2 is configured |
| `mshr_capacity` | uint32 | `8` | Maximum outstanding misses |
| `hit_rate` | double | `1.0` | Load hit probability (probabilistic mode) |
| `rng_seed` | uint64 | `0xBEEF` | Seed of the hit/miss RNG (probabilistic mode) |
| `cache_size_kb` | uint64 | `64` | Capacity in KB (structural mode) |
| `cache_line_size` | uint64 | `64` | Line size in bytes (structural mode) |
| `cache_associativity` | uint64 | `8` | Ways per set (structural mode) |
| `replacement_policy` | string | `lru` | `lru` or `plru` (structural mode) |
| `store_buffer_capacity` | uint32 | `8` | Store buffer entries (structural mode) |
| `write_policy` | string | `write_back` | `write_back` (write-allocate, dirty lines written back to the L2 on eviction) or `write_through` (structural mode) |
| `num_banks` | uint32 | `1` | Cache banks; `1` disables banking (structural mode) |
| `reads_per_bank_per_cycle` | uint32 | `1` | Read ports per bank (structural mode) |
| `writes_per_bank_per_cycle` | uint32 | `1` | Write ports per bank (structural mode) |
| `fill_occupies_write_port` | bool | `false` | Line fills consume a bank write port (structural mode) |

In structural mode, stores go to the store buffer and drain to the cache one
per cycle. In probabilistic mode, stores always hit.

### Writeback / ROB

`top.core0.writeback.params`

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `retire_width` | uint32 | `4` | Instructions retired per cycle |
| `rob_capacity` | uint32 | `128` | Reorder buffer entries |
| `retire_timeout_cycles` | uint32 | `10000` | Watchdog: abort the run when nothing retires for this many cycles |
| `log_enabled` | bool | `false` | Currently has no effect; use Sparta logging instead |

---

## L2 Cache

`top.core0.l2cache.params`

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `enabled` | bool | `false` | Connect a unified L2 behind the L1 caches |
| `hit_latency` | uint32 | `10` | L2 hit latency in cycles |
| `miss_latency` | uint32 | `100` | Additional latency of an L2 miss (memory) |
| `mshr_capacity` | uint32 | `16` | L2 MSHRs |
| `cache_size_kb` | uint64 | `1024` | Capacity in KB |
| `cache_line_size` | uint64 | `64` | Line size in bytes; keep equal to the L1 line size |
| `cache_associativity` | uint64 | `8` | Ways per set |
| `replacement_policy` | string | `lru` | `lru` or `plru` |
| `arb_policy` | string | `dcache_first` | Arbitration between I-cache and D-cache requests: `dcache_first`, `icache_first` or `round_robin` |
| `starvation_threshold` | uint32 | `8` | Cycles after which a waiting lower-priority request wins arbitration |
| `queue_capacity` | uint32 | `4` | Pending fill requests per source; the L1s hold off new misses while it is full |
| `inclusivity_policy` | string | `nine` | `nine` (non-inclusive, non-exclusive), `inclusive` (L2 evictions back-invalidate both L1s) or `exclusive` (currently behaves like `nine`) |

The L2 services one request per cycle. Only structural-mode L1 caches send
their misses to the L2.

---

## Advanced Microarchitectural Features

### Write-Port Arbiter

Models the limited number of physical register file write ports shared by the
execute groups and the LSQ (load results). Parameters live on Execute:

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `write_port_mode` | string | `unified` | `unified` or `mapped` |
| `write_port_count` | uint32 | `4` | Number of write ports (unified mode). `0` disables write-port arbitration |
| `write_port_mapping` | string list | `[]` | One entry per port (mapped mode): the `+`-separated sources that may use it, either execute-group indices (see [Execute](#execute)) or `lsq` |

In `unified` mode every port accepts every source; ports pick sources
round-robin. Example of a `mapped` configuration for the `functional`
granularity:

```yaml
top.core0:
  execute:
    params:
      granularity: functional
      write_port_mode: mapped
      write_port_mapping: ["0+1", "2+3", "lsq", "0+1+2+3+4+5+6+7+lsq"]
```

### Writeback Buffer

Buffers completed uops between Execute and the write-port arbiter. It only has
an effect while the write-port arbiter is enabled. Parameters live on Execute:

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `writeback_buffer_enabled` | bool | `false` | Enable the writeback buffer |
| `writeback_buffer_capacity` | uint32 | `16` | Buffer entries; Execute holds results while it is full |
| `writeback_buffer_drain_width` | uint32 | `4` | Entries offered to the arbiter per cycle |
| `writeback_buffer_latency` | uint32 | `1` | Cycles an entry stays in the buffer before it can be drained |

### Bypass Network and Speculative Wakeup

Parameters live on Issue:

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `speculative_wakeup` | bool | `false` | Wake up consumers of non-memory uops on a timer instead of on completion |
| `bypass_network_enabled` | bool | `false` | Enable the bypass network |
| `bypass_regfile_read_latency` | uint32 | `1` | Extra wakeup latency when no bypass path applies |
| `bypass_paths` | string list | `[]` | Bypass paths, one `producer:consumer:latency` entry each |

With `speculative_wakeup: true`, an issued non-memory uop marks its destination
registers ready after its execution latency plus a forwarding latency. That
forwarding latency is the smallest latency of any bypass path from the uop's
producer unit when the bypass network is enabled and such a path exists, and
`bypass_regfile_read_latency` otherwise. Loads always wake up their consumers
when the data returns. In the current model the bypass network is consulted
only for this wakeup timing.

Producer and consumer names in `bypass_paths` are `alu`, `mul`, `div`,
`branch`, `ldst`, `fp`, `vec` and `fence` (the indices of the `functional`
execute groups) plus `load` as a producer for LSQ results. `any` as a consumer
covers every unit. Use the `functional` execute granularity together with
bypass paths.

```yaml
top.core0:
  issue:
    params:
      speculative_wakeup: true
      bypass_network_enabled: true
      bypass_paths: ["alu:any:0", "mul:any:0", "load:any:1", "fp:fp:1"]
  execute:
    params:
      granularity: functional
```

### Register File Banking

Models a banked physical register file with a limited number of read ports per
bank. Physical register `p` lives in bank `p % regfile_num_banks`; a uop is only
selected for issue when every bank it reads from still has a free read port in
that cycle. Parameters live on Rename:

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `regfile_banking_enabled` | bool | `false` | Enable register file banking |
| `regfile_num_banks` | uint32 | `4` | Number of banks (at most 16) |
| `regfile_reads_per_bank` | uint32 | `2` | Read ports per bank per cycle |
| `regfile_writes_per_bank` | uint32 | `1` | Write ports per bank per cycle (currently not enforced) |

---

## Execution Driver

`top.core0.edriver.params` (the node exists only when `--target-elf` or
`--trace-file` is given)

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `target_command` | string | `""` | ELF to execute; set from `--target-elf` |
| `whisper_flags` | string list | `[]` | Extra command-line flags passed to Whisper |
| `snapshot_foldername` | string | `""` | Whisper snapshot to start from; set from `--trace-file` |
| `enable_snapshot_usage` | bool | `false` | Load the snapshot in `snapshot_foldername` |
| `allow_early_termination` | bool | `false` | Do not treat ending the run before the program finishes as an error |
| `log_enabled` | bool | `false` | Currently has no effect; use Sparta logging instead |

Whisper reads its configuration from `whisper.json` in the ELF's directory,
unless `whisper_flags` contains `--configfile`/`--config`. For example:

```bash
-p top.core0.edriver.params.whisper_flags "[--configfile,/path/to/whisper.json]"
```

---

## Visualization and Debugging

### Pipeline Visualizer

Records per-instruction pipeline timestamps and writes them at the end of the
run. Parameters live on `top.core0.params`:

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `visualizer_enabled` | bool | `false` | Enable the pipeline visualizer |
| `visualizer_max_instructions` | uint32 | `200` | Instructions to record (`0` = no limit) |
| `visualizer_format` | string | `table` | `table`, `waterfall`, `log` or `kanata` |
| `visualizer_output_file` | string | `stderr` | Output file, or `stderr` |
| `visualizer_color` | bool | `false` | ANSI colors in the `waterfall` format |
| `visualizer_streaming` | bool | `false` | Currently has no effect |
| `visualizer_debug_enabled` | bool | `false` | Also write a cycle-by-cycle waterfall, in pages of 20 cycles |
| `visualizer_debug_file` | string | `pipeline_debug.txt` | Output file of the debug waterfall |
| `visualizer_debug_start_cycle` | uint64 | `0` | First cycle of the debug waterfall |
| `visualizer_debug_end_cycle` | uint64 | `0` | Last cycle of the debug waterfall (`0` = end of run) |

Formats:

- `table`: one row per instruction with the cycle of each stage and notable
  events (I-cache miss, D-cache miss, misprediction, write-port stall).
- `waterfall`: stage occupancy per cycle, in pages of 32 cycles.
- `log`: `<cycle>;<id>;<event>;<data>` records for pipetrace viewers.
- `kanata`: Kanata log for the [Konata](https://github.com/shioyadan/Konata)
  pipeline viewer (`ext/konata`).

The debug waterfall is only produced while the visualizer is enabled.

### Cache Viewer

Traces requests, hits, misses, MSHR allocation, coalescing, fills and L2
arbitration of the structural-mode L1 caches and of the L2 (when enabled).
Parameters live on `top.core0.params`:

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `cache_viewer_enabled` | bool | `false` | Enable the cache viewer |
| `cache_viewer_format` | string | `log` | `log` (event list), `waterfall` (per-request timeline) or `summary`; `log` and `waterfall` also print the summary |
| `cache_viewer_output_file` | string | `stderr` | `stderr`, `stdout` or a file path |

### Logging

The per-unit `log_enabled` parameters currently have no effect. Use Sparta's
logging option instead, which enables the log messages of every unit under a
tree path:

```bash
# Writeback / retirement activity only
-l top.core0.writeback info wb.log

# Every unit
-l top info all.log
```

### Statistics and Diagnostics

- `--report-all FILE` writes every counter of the simulation tree at the end of
  the run (append `json` for JSON output).
- The pipeline clock prints a summary (IPC, simulation speed and per-unit
  counters) to stderr every 1,000,000 cycles and at the end of the run.
- When nothing retires for 1,000 cycles, the pipeline clock prints the state
  of the ROB, Issue, Execute, LSQ and Rename once to stderr.
- The ROB watchdog aborts the run after `writeback.params.retire_timeout_cycles`
  cycles without retirement.

---

## Example Configurations

The shipped configurations are a good starting point:

- `config.yaml`: out-of-order core with structural caches, `typed` execute
  groups, queues enabled and wrong-path speculation disabled.
- `config_inorder.yaml`: the same core with in-order execution.

The examples below are override files meant to be layered on top of
`config.yaml` with a second `-c`.

### Wrong-Path Speculation

```yaml
top.core0:
  params:
    wrong_path_enabled: true
```

### Partitioned Issue with an L2

```yaml
top.core0:
  issue:
    params:
      mode: partitioned
      scheduler_configs: ["int:16:ALU+Mul+Div+Branch+Fence", "mem:16:Load+Store", "fp:8:FpOp+VecOp"]
      routing_priorities: ["dependency_locality", "type_affinity", "least_occupied", "round_robin"]
      selection: fifo
  l2cache:
    params:
      enabled: true
      arb_policy: icache_first
```

### Detailed Backend

```yaml
top.core0:
  params:
    wrong_path_enabled: true
  rename:
    params:
      regfile_banking_enabled: true
  issue:
    params:
      speculative_wakeup: true
      bypass_network_enabled: true
      bypass_paths: ["alu:any:0", "mul:any:0", "load:any:1", "fp:fp:1"]
  execute:
    params:
      granularity: functional
      write_port_mode: mapped
      write_port_mapping: ["0+1", "2+3", "lsq", "0+1+2+3+4+5+6+7+lsq"]
  lsq:
    params:
      store_forwarding: true
  dcache:
    params:
      num_banks: 2
      fill_occupies_write_port: true
  l2cache:
    params:
      enabled: true
      inclusivity_policy: inclusive
      arb_policy: round_robin
```

### Pipeline Trace for Konata

```bash
./build/core/core -i 5000 -c models/cpu/src/core/config.yaml \
    -p top.core0.params.visualizer_enabled true \
    -p top.core0.params.visualizer_format kanata \
    -p top.core0.params.visualizer_max_instructions 0 \
    -p top.core0.params.visualizer_output_file pipeline.kanata \
    --target-elf tests/build/coremark.bare.elf
```
