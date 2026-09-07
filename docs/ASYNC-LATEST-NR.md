# Latest-completed NR (experimental)

Enable **Generate before SR, apply after SR (DLSS)** and **Async NR: use latest
completed result (experimental)** in the Neural Rendering panel:

```ini
[DlssNr]
Enabled=true
DeferredDLSS=true
AsyncLatest=true
```

This overrides ResidualFG and its two-frame sample/hold mode, without changing
their saved settings. It does not change the game's regular FG settings.

## Behaviour

The game produces its current raster normally. OptiScaler applies the most recent
completed, DLSS-upscaled NR residual to that current raster. The residual can be
several frames old and is not motion-warped to match. There is no one-frame raster
delay, no residual FG, and no raster-queue wait on NR completion.

Only one NR job is outstanding. Busy sampling requests are dropped rather than
queued. During initial warmup the raster continues clean; afterward the previous
completed residual remains in use until a newer one is ready. The status displays
residual age, completed jobs and dropped requests. Missing motion uses a private
zero guide. NR and private residual DLSS histories reset on each sparse snapshot;
the game upscaler's inputs and history are untouched.

Cuts, frame gaps and resolution/device/queue changes discard stale publication.
Depth is still required. Nonzero resource offsets, multiple upscales per frame,
native Vulkan and the separate RR path are not supported by this experiment.

## What is—and is not—asynchronous

NR and private residual DLSS **GPU execution** use a separate normal-priority
direct queue. The raster path polls completion; it never calls Queue::Wait or
waits for a CPU event from that queue. Input snapshots and final composition still
run on the raster queue. Three result slots prevent overwriting a published
residual while the raster GPU is reading it. GPU completion markers protect
snapshot readiness and consumer lifetimes; a fence protects worker completion.
The worker queue waits on a raster-side input-ready fence for explicit resource
handoff. That dependency is one-way: raster work never waits for the worker.

NVIDIA API calls and command-list recording remain serialized on the render CPU.
This is **not** a background CPU worker or a guarantee of unaffected frame pacing.
CPU model creation/rebuilds, snapshot copies, composition, VRAM pressure and shared
GPU compute/memory bandwidth still cost time. The change removes the dependency
on waiting for a fresh NR GPU result; it does not make NR free.

Regular frame generation receives the current raster and its original current
motion/depth, avoiding the previous delayed-raster/current-guide mismatch. Stale
NR detail can still trail moving objects or produce generated-frame artifacts.
Smooth pacing with real game workloads and driver FG must still be measured.

## Validation

- Release build and existing shared shader tests.
- `tests/nr_async_queue_smoke.cpp`: on NVIDIA hardware, three raster copies finish
  while an artificial gate stalls the worker queue; the worker finishes only
  after the gate is released. This is a queue scheduling test, not an NR model or
  complete game integration test.
- Existing shader tests verify applying a held residual to a changed current
  raster, rather than freezing the previous image.

The queue test uses a deliberate worker-only Wait to simulate a stall. The
production path has no raster-queue Wait on NR or blocking CPU fence event.
