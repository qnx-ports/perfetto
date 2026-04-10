# QNX Probe Known Issues List

This document describes the current know issues with the QNX Perfetto probe.

## QNX 8.0 Events out of order

### Status: Active

When running on QNX there is an issue the QNX tracelog buffers in which events
are erroniously assigned the incorrect CPU which results in them occasionally
appearing out of order. That is, when tagged with the wrong CPU the event
timestamp seems to have a regression.
We have introduced logic into
[traced_qnx_probe](../../../src/traced/qnx_probes/kernel/parser/trace_event_queue.cc) in order
to detect and heal such events but we are aware that there are occasionaly
events that appear as regressions or out of order to Perfetto. Occurrences are
typically rare and traces are still usable and generally intact.

## Frame Too Large

### Status: Fixed

There have been issues with the data sent via relay in which the buffer size is
being overrun. We used a temporary fix that increased the IPC buffer size to 1Mb
However we believe the issue is now fixed and the IPC buffer size has now been
reverted to 128k see [basic_types.h](../../../include/perfetto/ext/ipc/basic_types.h)