# Improved Tailslayer Spec

This document defines the improved Tailslayer spec derived from `ENGINEERING_RESUME.md`. It translates the research and engineering constraints into a stricter implementation and product contract for Intel, AMD, Graviton-class AArch64 systems, and other common architectures that can satisfy the same measurable requirements.

The governing rule is simple:

- if a capability is proven on the current machine, use it
- if a capability is not proven, do not guess
- if validation fails or the validated operating envelope collapses, demote to the baseline path

This fail-closed rule is what makes Tailslayer usable as a product rather than only as a research result.

## 1. Non-Negotiable Constraints

The product must preserve all of the following:

1. Do not predict DRAM refresh timing.
2. Do not rely on observed periodicity as a clock.
3. Use redundancy rather than prediction.
4. Keep hedge lanes off the same in-order retirement path.
5. Use explicit huge-page-class memory for placement control.
6. Discover placement empirically on each machine.
7. Treat hardware counters as optional accelerators, not as requirements.
8. Use first-result-wins semantics.
9. Keep synchronization out of the hot read path.
10. Treat memory and core overhead as explicit costs.
11. Fail closed when independence or operating conditions cannot be proven.

Any proposed optimization that violates one of those rules is out of scope.

## 2. Supported Target Model

The product targets these classes first:

- Intel x86_64
- AMD x86_64
- Graviton-class AArch64
- other AArch64 systems with explicit large-page allocation, stable high-resolution timing, and core affinity control

Support is capability-based, not vendor-name-based.

That means:

- Intel and AMD often get faster candidate generation because suitable counters may exist
- Graviton-class and other counterless systems remain first-class targets if timing-based discovery and validation are possible
- no backend may require a vendor-private counter or uncore interface as a condition of hedged mode

## 3. Runtime States and Capability Gating

The product operates in four states:

- `baseline`: normal single-read operation with no active validated hedge group
- `discovering`: control-plane discovery and validation in progress while requests remain on the baseline path
- `hedged`: validated hedge group active
- `degraded`: previously hedged configuration no longer satisfies its validated envelope

`hedged` mode is allowed only if the platform provides all of the following:

- explicit large-page or huge-page allocation with stable in-page offsets
- core affinity control
- enough distinct physical cores for the selected hedge width
- a stable, ordered, high-resolution timer suitable for paired latency measurements
- a validated hedge group whose target-tail and co-stall behavior beat baseline and the matched negative control

If any one of those checks fails, the system must remain on or demote to `baseline` mode.

Discovery must never steal production hedge cores. If the system does not have spare control-plane resources, discovery must block activation rather than competing with production worker lanes.

The discovery budget must bound at least:

- control-plane cores consumed by probing
- temporary memory consumed by candidate arenas and statistics buffers
- wall-clock time or retry budget before discovery yields or aborts

## 4. Execution Contract

Tailslayer is only valid if the execution model preserves independence through request delivery, memory access, and completion.

### 4.1 Worker model

The steady-state execution model is:

- one worker per hedge lane
- one hedge lane per replica
- one hedge lane per dedicated physical core
- request delivery before the worker hot path begins
- read plus compute inside the worker
- winner suppression after the worker hot path

The worker callback must be treated as a pure compute stage or an idempotent candidate-construction stage. Irreversible side effects must happen only after deduplication.

### 4.2 Request dispatch must be broadcast

Request delivery to hedge lanes must be broadcast or multicast, never one-consumer dequeue semantics.

Acceptable implementations:

- NIC or kernel fan-out that delivers identical packets to multiple worker inputs
- one publication per worker lane
- one SPSC queue per lane carrying identical logical requests

Forbidden implementations:

- one shared MPMC queue for all hedge lanes
- any design where successful consumption by one worker removes the request from the others

If shared memory is used for fan-out, publication must establish a proper happens-before edge. The portable default is release on enqueue and acquire on dequeue per lane.

### 4.3 Winner selection must remain outside the hot path

Worker lanes must never coordinate with one another to determine the winner through shared memory on the critical path.

Allowed winner models:

- external arbitration, such as NIC, gateway, or downstream deduplication
- temporal first-arrival semantics where the earliest completed result wins naturally outside the worker domain

Forbidden winner models:

- shared winner flags
- shared compare-and-swap winner election in the worker path
- any lock or atomic touched by all racing lanes before completion

## 5. Topology and Isolation Rules

### 5.1 Physical-core requirements

Each hedge lane must own its own physical execution resources.

Required rules:

- one hedge lane per dedicated physical core
- no SMT siblings inside the same hedge group
- no mixed core classes inside one hedge group unless that exact mixture is validated and fingerprinted as a separate configuration
- no cross-NUMA hedge groups by default

Cross-NUMA hedging is out of scope for the default portable product because it can raise baseline latency materially and obscure whether hedging actually helped.

### 5.2 Control-state isolation

Per-lane mutable control structures must never share cache lines.

This applies at minimum to:

- ingress metadata
- health counters
- mutable worker control state

The goal is to prevent false sharing from reintroducing cross-core interference into a supposedly independent hedge group.

### 5.3 Non-portable placement assumptions are not allowed

Page coloring and cache-slice targeting are not portable requirements.

They may exist as platform-specific experiments, but they must not be treated as baseline portable assumptions or as prerequisites for correctness.

## 6. Replica Memory and Update Model

### 6.1 Replica arena requirements

The replica arena must be explicitly controlled.

Required rules:

- use explicit huge-page-class allocation, not transparent huge pages as proof of placement
- prefault the arena before activation
- place the arena on the NUMA node used by the hedge group
- fail closed if the requested huge-page reservation cannot be satisfied

Huge-page pool pressure is therefore an activation-time resource check, not a reason to guess at fallback placement.

### 6.2 Active replica model

The default active replica model is immutable snapshots.

Required rules:

- replicas are fully built before activation
- active replicas are read-only during steady-state reads
- in-place per-entry mutation is forbidden in the worker hot path

When the platform allows it, active replica memory should be mapped read-only after initialization.

### 6.3 Whole-snapshot replacement

If data must change, the default update model is whole-snapshot replacement:

1. Build a fresh full replica set off the hot path.
2. Validate the new layout if placement changed.
3. Publish the new snapshot from the control plane.
4. Retire the old snapshot only after a quiescent point.

Allowed quiescent methods in the portable product are:

- explicit pause and resume between requests
- epoch boundary acknowledged outside the worker hot read path

### 6.4 Pending snapshot bound

The product must bound unpublished snapshots.

Required rule:

- each hedge group must have an explicit pending-snapshot bound

Portable default:

- at most one unpublished snapshot may be pending per hedge group

Overflow policy must be explicit and configured as one of:

- `reject-new`
- `drop-old-pending`
- `coalesce-to-latest`

No implementation may allow unbounded snapshot accumulation while waiting for quiescent publication.

## 7. Discovery Pipeline

The product must separate discovery from execution.

### 7.1 Machine fingerprint

Before any layout is reused or discovered, record a machine fingerprint that includes at least:

- architecture
- vendor
- family, model, and stepping when available
- microcode or firmware revision when available
- kernel version
- NUMA topology
- SMT and hybrid-core topology
- page size class used for replicas
- timer source used for validation
- CPU governor or equivalent frequency policy
- mixed-core placement policy when heterogeneous cores exist
- IOMMU or VT-d or SMMU mode when ingress duplication depends on DMA or NIC-backed delivery

A stored layout may be reused only when the fingerprint matches exactly.

### 7.2 Candidate generation

Candidate offsets may be generated by either of these methods:

- hardware-counter prefiltering on platforms that expose suitable counters
- timer-based bit or offset sweeps on platforms that do not

Important rules:

- counters are a prefilter only
- counters must never be treated as a requirement for hedged mode
- timer-based discovery must remain first-class on counterless or black-box platforms such as Graviton

On counterless platforms, timing is both the probe and the proof:

- generate candidate offsets by timed probing
- use hedged-read improvement as the discovery signal
- use the full statistical validation path as the acceptance proof

Even on platforms with counters, final acceptance still requires the same paired latency validation and co-stall analysis as every other platform.

## 8. Quantitative Validation Contract

A candidate hedge group is accepted only after quantitative paired-latency validation.

### 8.1 Default validation target

Unless the workload config specifies a stricter or more relevant target, the default target percentile is:

- P99.99 for latency acceptance

The workload may choose a stricter or more relevant target, but the choice must be explicit.

### 8.2 Minimum sample requirement

Candidate acceptance requires at least:

- 1,000,000 paired reads per candidate configuration

The sample set must be divided into repeated blocks so confidence intervals are computed from repeated observations rather than one monolithic run. A portable default is at least 20 blocks.

### 8.3 Confidence requirement

Accept or reject decisions must use:

- 95% confidence intervals

The implementation may use block bootstrap, repeated-block resampling, or another statistically valid method, but the method must be applied consistently across candidates.

### 8.4 Tail-improvement criterion

For the chosen target percentile $p_t$:

- let $Q_b(p_t)$ be the baseline target-percentile latency
- let $Q_h(p_t)$ be the hedged target-percentile latency of the winning minimum

The candidate passes the tail-improvement test only if:

- the upper bound of the 95% confidence interval for $Q_h(p_t)$ is below the lower bound of the 95% confidence interval for $Q_b(p_t)$

### 8.5 Co-stall criterion

For a threshold $t$ chosen from the baseline tail target, estimate joint-tail behavior:

- for two lanes, $p_{AB}(t) = P(lat_A > t \land lat_B > t)$
- for wider hedge groups, estimate the probability that all active lanes exceed $t$ and pairwise joint-tail probabilities for every added lane against at least one already-accepted lane

The candidate passes the co-stall test only if:

- the upper bound of the candidate joint-tail confidence interval is below the lower bound of the matched negative control confidence interval

### 8.6 Negative controls are mandatory

Every validation run must include a matched correlated control.

Preferred control:

- a known same-channel or same-correlated-path pair from the same arena and NUMA node

Fallback on black-box platforms:

- the most correlated matched pair discovered during probing, used explicitly as the negative control

This is especially important on counterless platforms such as Graviton, where timing is both the probe and the proof.

### 8.7 Typical-latency budget

The candidate must show no statistically significant regression at median or near-median latency beyond measurement error unless the workload config explicitly allows a typical-latency budget in exchange for a larger tail gain.

### 8.8 Validation must test more than refresh correlation

The joint-tail analysis must be treated as protection against all correlated delay sources visible in the measured path, including:

- refresh collisions
- controller queue contention
- OS or scheduler noise
- interrupt noise
- thermal or power-state coupling

The product only cares whether stalls co-occur on the measured path, not which subcomponent caused them.

### 8.9 Resource-fit rule

The candidate must fit the configured:

- core budget
- memory budget
- discovery and health-sampling budget

If any one of the validation checks fails, the candidate must be rejected.

## 9. Effective Hedge Width and Admission Control

### 9.1 Effective width is empirical

Effective hedge width is determined by validated independence and measured improvement, not by nominal channel count.

The default search policy is conservative:

1. Validate width 2 first.
2. Validate wider candidates one step at a time.
3. Stop at the minimum width that satisfies the configured target.

An implementation may use coarse-to-fine candidate ordering to reduce search time, but it may not assume monotonic improvement with width. Every accepted width still requires explicit validation.

### 9.2 Admission rule for real workloads

Tailslayer should be enabled only for data and requests that are both:

- DRAM-dominant in practice
- unpredictable enough that ordinary caching or prefetching does not solve the problem

If a table is small enough to stay hot in cache, or if access is predictable enough for ordinary prefetching to dominate, the product should keep that table on the baseline path.

Implementations may use machine-evaluable prefilters to skip unnecessary discovery, such as:

- observed tail-to-median ratio
- LLC miss rate or other evidence that the working set is not cache-resident

These prefilters may skip discovery when the workload obviously does not qualify, but they may not force acceptance. Direct paired validation remains binding.

## 10. Operating Environment Requirements

The product must account for OS and firmware reality.

Required rules:

- validation and deployment must run under a stable governor or equivalent frequency policy
- interrupt affinity should avoid hedge cores when the OS allows it
- hedge cores must remain pinned and must not be repurposed by discovery or background validation
- validation must be repeated under representative system load, not only on an unnaturally quiet machine

Thermal throttling, power policy changes, noisy neighbors, interrupt routing, and scheduler interference are treated as first-class degradation sources, not as unexplained noise.

## 11. Health Verification and Degradation Handling

Health verification must run only off the worker hot path.

Required behavior:

- run low-duty-cycle validation samples on a control thread or non-worker core
- compare the live system against the previously validated performance envelope, not merely against baseline
- if the validated envelope is no longer satisfied, mark the hedge group degraded and either revalidate, rediscover, or demote to baseline according to policy

A hedge group may therefore be marked degraded even when it is still somewhat better than baseline, if it no longer satisfies the envelope it was accepted under.

### 11.1 Degradation signaling

The health API must return a reasoned degradation state:

```text
Healthy
Degraded(reason)
```

Where `reason` is at least one of:

- `fingerprint_mismatch`
- `layout_validation_failed`
- `joint_tail_regression`
- `typical_latency_regression`
- `lost_core_affinity`
- `governor_or_frequency_change`
- `thermal_or_power_throttle`
- `interrupt_or_noise_budget_exceeded`
- `hugepage_resource_failure`
- `timer_source_changed`
- `unknown`

This allows the caller to distinguish between full rediscovery, targeted revalidation, and environmental wait-and-retry.

## 12. Control-Plane API

The portable control-plane API is:

```text
probe_capabilities() -> CapabilityReport
discover_layout(config) -> CandidateLayouts
validate_layout(candidate) -> ValidatedLayout | Rejected
activate_snapshot(snapshot, layout) -> Mode
sample_health(layout) -> Healthy | Degraded(reason)
deactivate(mode) -> Drained
```

Semantics:

- `probe_capabilities` decides whether hedged mode is even possible
- `discover_layout` runs only in `discovering` state
- `validate_layout` is the only stage allowed to approve a hedge group
- `activate_snapshot` enables either `baseline` or `hedged` operation
- `sample_health` may move a hedge group into `degraded` or trigger demotion to `baseline`
- `deactivate` drains workers and releases reserved cores and memory explicitly

## 13. Explicitly Rejected Techniques

The following ideas are deliberately excluded because they violate the portable contract or create regression risk:

- refresh prediction or schedule avoidance
- hardcoded offsets such as always using 256 bytes
- shared winner flags in the worker hot path
- lock-based or atomic arbitration between hedge lanes
- one-consumer dequeue semantics for hedge-lane dispatch
- in-place mutable replicas during steady-state reads
- transparent huge pages as sole proof of stable placement
- cross-NUMA hedge groups by default
- blind mixing of SMT siblings or heterogeneous core classes
- vendor-private counters as the only proof of independence
- enabling wider hedges without measured additional gain
- speculative prefetch as the core mechanism for the same workload class

These are not conservative extensions. They are regression risks.

## 14. Minimal Correct Improved System

According to this spec, a minimal correct improved Tailslayer system must do all of the following:

1. Allocate replicas in an explicitly controlled huge-page arena.
2. Broadcast identical logical requests to all hedge lanes.
3. Pin one hedge lane to one dedicated physical core.
4. Keep active replicas immutable during steady-state reads.
5. Keep winner selection outside the worker hot path.
6. Treat hardware counters as optional prefilters only, and support timer-based discovery on counterless platforms such as Graviton.
7. Validate candidate groups with paired measurements, negative controls, at least 1,000,000 paired reads, repeated-block confidence estimation, and 95% confidence intervals.
8. Accept a hedge group only when both target-tail latency and joint-tail behavior beat baseline and the matched negative control without unauthorized typical-latency regression.
9. Treat effective hedge width as an empirical result, not a channel-count assumption.
10. Operate under stable power, timer, interrupt, and affinity conditions, or fail closed.
11. Bound pending snapshots and define overflow behavior explicitly.
12. Surface degradation reasons explicitly and demote safely when the validated envelope no longer holds.

If any one of those conditions is missing, the system is no longer the improved Tailslayer product described by the research and refined by the engineering resume.

## 15. Final Product Statement

The safest better spec is not a more aggressive Tailslayer. It is a stricter one.

This version improves the product contract in the exact places that would otherwise cause silent divergence between implementations:

1. Broadcast dispatch replaces ambiguous request duplication.
2. External or temporal winner semantics replace shared arbitration.
3. Quantitative validation replaces vague acceptance language.
4. Counterless platforms such as Graviton are handled explicitly, not implicitly.
5. Snapshot publication is bounded.
6. Discovery, degradation, and teardown are explicit control-plane concepts.

That is the highest-certainty portable product path consistent with `RESEARCHER_COMMENTARIES.md` and `ENGINEERING_RESUME.md`.
