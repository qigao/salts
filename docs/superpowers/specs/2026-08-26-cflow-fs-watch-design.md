# CFlow Filesystem Watch Design

Issue: [#113](https://github.com/qigao/turbo-utils/issues/113)
Parent: [#111](https://github.com/qigao/turbo-utils/issues/111)

## Decision

Extend `TurboUtils::CFlowFS` with an opaque watch source and platform bridges:
inotify on Linux, overlapped `ReadDirectoryChangesW` on Windows, and FSEvents on
macOS. The portable contract reports facts common to those sources without
inventing a stronger per-file or rename guarantee.

## Event contract

Portable kinds are created, removed, modified, attributes, renamed,
root-changed, and rescan-required. An event owns bounded relative-path storage
until its callback returns. Backends may populate both old and new paths only
when a native correlation token proves the pair; otherwise they emit the
observable unpaired events.

Recursive mode is explicit. Linux maintains one inotify watch per discovered
directory; Windows uses the native subtree flag; macOS FSEvents is naturally
recursive and directory-coalesced. Backend identity and platform capability
are observable.

## Loss and recovery

Kernel overflow, user queue saturation, dropped FSEvents flags, an unpairable
rename sequence, or an event whose path exceeds configured storage transitions
the source to `RESCAN_REQUIRED`. Exactly one coalesced rescan event is retained;
detailed events are suppressed until the driver acknowledges the rescan. The
library never silently claims a complete stream after loss.

## Ownership and shutdown

The source owns its native handles, backend thread or run loop, watch registry,
fixed event slots, and path storage. Native callbacks only publish into the
bounded queue; user callbacks run on the single CFlowFS driver. Close stops new
native publication, cancels or wakes the backend wait, drains already accepted
events, and then permits destroy. Root deletion produces `ROOT_CHANGED` before
terminal close.

`event_capacity` bounds detailed event slots, while one allocation-free
emergency rescan marker is retained in the control state. `watch_capacity`
bounds native directory registrations for recursive backends. `path_capacity`
bounds each normalized UTF-8 relative path, and `native_buffer_capacity`
bounds one kernel read buffer.

## Compatibility and validation

This is additive to `TurboUtils::CFlowFS`; neither `TurboUtils::Core` nor core
CFlow gains a dependency. Platform tests cover create/modify/rename/remove,
recursive discovery, overflow/rescan acknowledgement, root removal, close
races, and fixed-capacity reuse. Each backend is validated on its native OS.
