# QNX Probe Known Issues List

This document describes the current known issues with the QNX Perfetto probe.

## QNX 8.0 Timestamp Regressions

**Status**: *Active*

When running on QNX 8.0 there is a known issue where events occasionally occur
out of order for a particular CPU. The QNX tracelog callback delivers kernel
trace buffers that are:

- Uniform in CPU: All the events in a buffer are from the same CPU (with
exception of priority inheritance events which we detect and ignore).
- Ordered by time: Events in a buffer are from earliest to latests where the
event timestamps are always incrementing

For this known issue, traced_qnx_probes occasionally sees events with timestamp
regressions. We also see, upon import into the Perfetto UI, error with the
perfetto trace that indicate events out of order. Occurrences are typically rare
and traces are still usable and generally intact and useful.

## Perfetto Trace Import Errors

**Status**: *Active*

When importing Perfetto traced files that include data from traced_qnx_probes
into the Perfetto UI we occasionally see reports of *IMPORT ERRORS* with details
of *generic_task_state_invalid_order*.

## Priority Inheritance Events

**Status**: *Fixed*

When parsing QNX 8.0 tracelog buffers, traced_qnx_probes occasionally sees
events that are tagged with a different CPU. These are expected events that
indicate a priority inheritance. Such events are ignored by traced_qnx_probes
since the associated thread events will carry the priority (in wide mode) and
the priority inheritance event is redundant.

## Frame Too Large

**Status**: *Fixed*

There have been issues with the data sent via relay in which the buffer size is
being overrun. We used a temporary fix that increased the IPC buffer size to 1Mb
However we believe the issue is now fixed and the IPC buffer size has now been
reverted to 128k see [basic_types.h](../../../include/perfetto/ext/ipc/basic_types.h)
