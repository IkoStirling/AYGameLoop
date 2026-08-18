# AYGameLoop Frame Stage Model

Status: accepted design target for the next GameLoop revision.

## Decision

The loop uses explicit phase barriers. Dependency edges no longer imply an
execution phase, and lifecycle dependencies are kept separate from per-frame
ordering.

`basePriority` remains only as a deterministic tie-breaker inside one phase.
It must never move work across a phase barrier.

## Frame sequence

```text
FrameBoundary
  wait for previous render; pump cross-thread completions; sample clocks
Platform
  poll window, input, device hot-plug
Ingress
  pump platform events; receive network/resource/file-watcher completions

repeat for every due game fixed step:
  FixedPrePhysics
    apply authoritative input/network commands; deterministic scripts/gameplay
  FixedPhysics
    advance physics and publish its snapshot
  FixedPostPhysics
    consume contacts; synchronize transforms; publish SimTickEvent
  FixedEventCheckpoint
    deliver events produced by the completed simulation step

Gameplay
  variable-rate non-authoritative scripts and game presentation logic
World
  animation, state machines, cameras, transforms, render extraction
Presentation
  audio, video and renderer-side preparation
Egress
  replication/RPC output produced from the completed world state
FrameEnd
  emit LogicFrameEnd; submit immutable render packet; apply frame pacing
```

The render callback consumes only the immutable packet produced at `FrameEnd`.
It must not read mutable ECS or subsystem state concurrently.

## Clock domains

Clock selection is independent of phase selection.

- `Game`: affected by pause and `timeScale`. The game accumulator receives
  `wallDelta * timeScale`; each fixed simulation step still receives the
  constant configured `fixedDelta`.
- `Unscaled`: clamped wall time, used by UI, editor presentation, audio and
  video.
- `RealWall`: monotonic, unclamped wall time for transport timeouts and
  diagnostics. It must not drive deterministic simulation.

Smoothed delta is presentation-only. It is forbidden for fixed simulation,
network authority, replay and save-state timestamps.

## Descriptor contract

The target descriptor separates three concepts:

```cpp
struct SubSystemDescriptor {
    const char* name;
    std::vector<const char*> initializeAfter;
    PhaseMask phases;
    ClockDomain clock;
    int32_t phasePriority;
};
```

A subsystem may participate in multiple phases. Its callback receives the
current phase and a read-only `FrameContext`. Dependencies used for lifecycle
validation never silently become frame-order dependencies.

Within a phase, explicit `runsAfter` edges are allowed. An edge targeting a
later phase is redundant; an edge targeting an earlier phase is a startup
error. Missing dependencies and cycles are startup errors rather than silently
ignored hints.

## Initial subsystem placement

- Device: `Platform`, Unscaled.
- Network: `Ingress` and `Egress`, RealWall; deterministic replication apply
  occurs in `FixedPrePhysics` using queued packets.
- Script: deterministic hosts in `FixedPrePhysics`; ambient/visual hosts in
  `Gameplay`. The same script host must not be ticked in both lanes.
- Project gameplay subsystems: `FixedPrePhysics` when authoritative, otherwise
  `Gameplay`; their existing dependency chain remains inside that phase.
- Physics: `FixedPhysics`, Game.
- Entity: `FixedPostPhysics` for physics-to-transform synchronization and
  `World` for variable animation/camera/render extraction.
- Audio: `Presentation`, Unscaled.
- Video: `Presentation`, Unscaled, after Audio.
- Renderer subsystem preparation: `Presentation`, after Entity; actual render
  execution remains outside the logic phase graph.

## Event checkpoints

The main thread pumps queued events at four defined points only:

1. `FrameBoundary` for worker/render completions.
2. Between `Platform` and `Ingress` so input/window edges are visible in the
   same frame.
3. After every `FixedPostPhysics` so collision and simulation events cannot
   leak into an arbitrary later fixed step.
4. Before `FrameEnd` for presentation/egress notifications.

Synchronous `emit` remains main-thread-only. Cross-thread producers always
use `post`.

## Pause behavior

Paused frames continue `FrameBoundary`, `Platform`, `Ingress`, `Presentation`,
`Egress`, and `FrameEnd`. Game-clock phases (`Fixed*`, `Gameplay`, `World`) are
skipped. This keeps windows, UI, media, networking and editor controls
responsive without advancing the game simulation.

## Migration order

1. Add phase and clock types plus validation while retaining the legacy
   `update/fixedUpdate` adapter.
2. Change timing to a scaled game accumulator and expose immutable
   `FrameContext`.
3. Split Network ingress/egress and Script deterministic/ambient callbacks.
4. Split Entity fixed synchronization from variable world presentation.
5. Move event pumps to the defined checkpoints.
6. Remove legacy `basePriority`, `TimeType`, `update` and `fixedUpdate` after all
   runtime modules migrate.

