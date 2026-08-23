# Performance Issues: Real-Time Audio Budget and Graph Scheduling

## The Problem

Transmission pre-renders audio blocks on a worker thread and feeds a bounded queue to the JACK callback. When the worker thread cannot process one block before the JACK deadline arrives for the next, the queue empties and the engine outputs silence. This sounds like the piece "stops" after a few seconds.

The deadline at 2048 samples / 48 kHz is **42.67 ms per block**. If the combined processing time for all nodes exceeds that budget, the queue depletes faster than it is refilled, regardless of how large the render-ahead buffer is set.

## Root Cause: Sequential Level Bottlenecks

The graph scheduler performs a BFS-based topological sort that groups nodes into parallel *execution levels*. Nodes within the same level may run concurrently; nodes in different levels are always sequential (a later level cannot start until the earlier level completes).

For the crocotta project the three most expensive plugins happened to fall into *different* sequential levels:

| Level | Dominant node | Cost    |
|-------|---------------|---------|
| 1     | Floozy        | 20.3 ms |
| 2     | Basilico      | 9.1 ms  |
| 3     | Ambo          | 14.3 ms |

Because each level is gated on the previous one, increasing `processingThreads` provides no benefit: the critical path (sum of per-level maxima) equals the serial sum when every level is dominated by a single node. With all three bottlenecks sequential the total was 50.6 ms — 19% over budget.

The `renderAheadBlocks` buffer only delays the onset of silence; it does not change the steady-state ratio. At a render/real-time ratio of 1.19×, a 24-block buffer depletes in roughly 130 blocks (~5.5 s of output), matching the observed symptom.

## Short-Term Fix Applied

Ambo (reverb on Campione's animal-sound output) was removed from the graph and Campione wired directly to T-Mix. This saved 14.3 ms and brought the critical path to ~36.3 ms, within the 42.67 ms deadline. Animal sounds still pass through Lightverb downstream.

## Long-Term Recommendations

### 1. Profile plugins before patching

Before assembling a graph, measure per-node average and worst-case render time with:

```sh
node scripts/probe-project.js projects/<name>.ttl --seconds 2 --block-size 2048
```

Nodes that individually consume more than ~10 ms (≈25% of the 42.67 ms budget) are risky: even one such node per sequential level can exhaust the budget. Treat anything over 15 ms as a hard constraint that must be placed at the same level as other expensive nodes, or replaced with a lighter alternative.

### 2. Restructure graphs to maximise parallel width

The scheduler assigns a node to the earliest level whose dependencies are all satisfied. Expensive nodes that depend on inexpensive MIDI-only predecessors (e.g. Floozy depending only on Polymeter, Basilico depending only on Ground's MIDI output) end up in levels that run sequentially even though there is no audio data dependency between them.

Where the musical relationship allows it, move MIDI clock signals upstream so that expensive synthesis nodes share a level. For example, if Floozy and Basilico could both depend on a common, cheap root node they would run in the same level, with the level cost being max(Floozy, Basilico) rather than Floozy + Basilico.

### 3. Reduce voice counts for polyphonic synths

Polyphonic synthesis cost scales roughly linearly with voice count. Floozy at 4 voices consumed 20.3 ms; 2 voices should bring it to ~10 ms. For complex patches, lower `num_voices` parameters on heavy synths until the probe shows each level fits within budget.

### 4. Add a budget warning to the probe output

`vst3_project_probe_main.cpp` already emits per-node timing. Extend it to compute the critical path across levels and emit a `BUDGET` line:

```
BUDGET criticalPathUs=50640 deadlineUs=42667 ratio=1.187 OVER_BUDGET
```

This makes budget violations visible before a patch is loaded into the live engine.

### 5. Expose level assignments in diagnostics

The `diag` console command shows per-node averages but not which execution level each node belongs to. Adding level IDs would make it immediately obvious when expensive nodes are in separate levels, enabling faster diagnosis without needing to mentally reconstruct the BFS ordering.

### 6. Consider a separate render-ahead probe mode

A future `probe --realtime` mode could run the graph against JACK with a short render-ahead (e.g. 4 blocks) and report underrun ratio over 10 seconds. This catches timing margin issues that offline measurement misses (OS scheduling jitter, plugin internal threading, PipeWire quantum renegotiation after activation).
