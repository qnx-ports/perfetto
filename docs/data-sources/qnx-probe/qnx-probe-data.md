# QNX Probe Data Collection

***traced_qnx_probes*** integrates with the QNX kernel trace logging system in
order to capture tracing events that describe what is going on within the target
system.

The set currently captured by the probe includes:

## Process Information

*traced_qnx_probes* captures QNX kernel trace information about the running
processes, including:

- **pid**: Unique identifier of the process within the system.
- **ppid**: Identifier of the process that created this process.
- **cmdline**: Full command (including arguments) used to start the process.
- **threads**: The threads associated with the process.
- **status**: CREATED, NAMED, DESTROYED.

## Thread Information

*traced_qnx_probes* also captures information about process threads, including:

- **tid**: Unique identifier of a thread within the system.
- **name**: Name assigned to the thread.
- **cpu**: CPU currently associated with the thread.
- **prio**: Thread priority.
- **status**: The current state of the thread.

### Thread Status

QNX thread states are mapped to the following states in perfetto.

- **CREATED**: Thread has been created but has not started execution.
- **NAMED**: Thread has been assigned a name.
- **RUNNABLE**: Thread is ready to run and waiting to be scheduled on a CPU.
- **RUNNING**: Thread is actively executing on a CPU.
- **INTERRUPTIBLE_SLEEP**: Thread is blocked waiting for an event, signal,
or timeout and may be resumed when the condition changes.
- **UNINTERRUPTIBLE_SLEEP**: Thread is blocked waiting for a kernel-managed
resource or operation that must complete before resuming.
- **STOPPED**: Thread execution has been suspended.
- **DEAD**: Thread has terminated and is awaiting resource cleanup or join.
- **DESTROYED**: hread has been fully cleaned up and all associated
resources have been released.

## Generic Kernel Events

In order to support data from operating systems other than Linux the Perfetto
system has been augmented with *"generic"* event structures that are not OS
specific.

*traced_qnx_probes* captures QNX tracing events and converts them into
generic Perfetto structures in order to inject the QNX trace data into Pefetto.

For detailed information about the events collected by the QNX probe see
[GenericTaskEvent Protos](../../../protos/perfetto/trace/generic_kernel).

## Mapping QNX Thread State to Perfetto

The following table describes how QNX kernel events are mapped into generic
Perfetto kernel events.

### QNX 7.1 Mappings

| QNX 7.1 Kernel Event | Perfetto Generic Kernel Event     |
|----------------------|-----------------------------------|
|STATE_RUNNING         | TASK_STATE_RUNNING                |
|STATE_READY           | TASK_STATE_RUNNABLE               |
|STATE_STOPPED         | TASK_STATE_STOPPED                |
|STATE_SEND            | TASK_STATE_INTERRUPTIBLE_SLEEP    |
|STATE_RECEIVE         | TASK_STATE_INTERRUPTIBLE_SLEEP    |
|STATE_REPLY           | TASK_STATE_INTERRUPTIBLE_SLEEP    |
|STATE_STACK           | TASK_STATE_INTERRUPTIBLE_SLEEP    |
|STATE_WAITTHREAD      | TASK_STATE_INTERRUPTIBLE_SLEEP    |
|STATE_SIGSUSPEND      | TASK_STATE_INTERRUPTIBLE_SLEEP    |
|STATE_SIGWAITINFO     | TASK_STATE_INTERRUPTIBLE_SLEEP    |
|STATE_NANOSLEEP       | TASK_STATE_INTERRUPTIBLE_SLEEP    |
|STATE_MUTEX           | TASK_STATE_INTERRUPTIBLE_SLEEP    |
|STATE_CONDVAR         | TASK_STATE_INTERRUPTIBLE_SLEEP    |
|STATE_JOIN            | TASK_STATE_INTERRUPTIBLE_SLEEP    |
|STATE_INTR            | TASK_STATE_INTERRUPTIBLE_SLEEP    |
|STATE_SEM             | TASK_STATE_INTERRUPTIBLE_SLEEP    |
|STATE_WAITCTX         | TASK_STATE_INTERRUPTIBLE_SLEEP    |
|STATE_NET_SEND        | TASK_STATE_INTERRUPTIBLE_SLEEP    |
|STATE_NET_REPLY       | TASK_STATE_INTERRUPTIBLE_SLEEP    |
|STATE_WAITPAGE        | TASK_STATE_UNINTERRUPTIBLE_SLEEP  |
|STATE_DEAD            | TASK_STATE_DEAD                   |

### QNX 8.0 Mappings

| QNX 8.0 Kernel Event | Perfetto Generic Kernel Event     |
|----------------------|-----------------------------------|
|STATE_CREATE          | TASK_STATE_CREATED                |
|STATE_RUNNING         | TASK_STATE_RUNNING                |
|STATE_READY           | TASK_STATE_RUNNABLE               |
|STATE_STOPPED         | TASK_STATE_STOPPED                |
|STATE_SEND            | TASK_STATE_INTERRUPTIBLE_SLEEP    |
|STATE_RECEIVE         | TASK_STATE_INTERRUPTIBLE_SLEEP    |
|STATE_REPLY           | TASK_STATE_INTERRUPTIBLE_SLEEP    |
|STATE_MQ_SEND         | TASK_STATE_INTERRUPTIBLE_SLEEP    |
|STATE_MQ_RECEIVE      | TASK_STATE_INTERRUPTIBLE_SLEEP    |
|STATE_SIGSUSPEND      | TASK_STATE_INTERRUPTIBLE_SLEEP    |
|STATE_SIGWAITINFO     | TASK_STATE_INTERRUPTIBLE_SLEEP    |
|STATE_NANOSLEEP       | TASK_STATE_INTERRUPTIBLE_SLEEP    |
|STATE_MUTEX           | TASK_STATE_INTERRUPTIBLE_SLEEP    |
|STATE_CONDVAR         | TASK_STATE_INTERRUPTIBLE_SLEEP    |
|STATE_JOIN            | TASK_STATE_INTERRUPTIBLE_SLEEP    |
|STATE_INTR            | TASK_STATE_INTERRUPTIBLE_SLEEP    |
|STATE_SEM             | TASK_STATE_INTERRUPTIBLE_SLEEP    |
|STATE_RWLOCK_READ     | TASK_STATE_INTERRUPTIBLE_SLEEP    |
|STATE_RWLOCK_WRITE    | TASK_STATE_INTERRUPTIBLE_SLEEP    |
|STATE_BARRIER         | TASK_STATE_INTERRUPTIBLE_SLEEP    |
|STATE_PIPE            | TASK_STATE_INTERRUPTIBLE_SLEEP    |
|STATE_MUON_MUTEX      | TASK_STATE_INTERRUPTIBLE_SLEEP    |
|STATE_TRACEBUFFER     | TASK_STATE_INTERRUPTIBLE_SLEEP    |
|STATE_INTR_ATTACH_EV  | TASK_STATE_INTERRUPTIBLE_SLEEP    |
|STATE_TIMER_DELEGATE  | TASK_STATE_INTERRUPTIBLE_SLEEP    |
|STATE_WAITPAGE        | TASK_STATE_UNINTERRUPTIBLE_SLEEP  |
|STATE_DEAD            | TASK_STATE_DEAD                   |

QNX has extensive support for tracing with many events. For a detailed
description see
[Table of QNX Trace Events](https://www.qnx.com/developers/docs/8.0/com.qnx.doc.sat/topic/kercall_table_Events.html).

## Mapping QNX Thread IDs to Perfetto

Perfetto and QNX have different paradigms for tracking thread IDs (tids). The
Perfetto follows the Linux paradigm in which tids are globally unique on the
system.

However, in the QNX paradigm threads associated to a process such that tids are
only unique within a process. Moreover, tids on QNX are aggressively
recycled.

In order to map QNX tids into perfetto, the QNX process ID (pid) and the QNX tid
are combined to create a single globally unique value. The perfetto tid has the
QNX pid as the most significant 32 bits and the tid as the least significant 32
bits.

| [63] QNX pid (32 bits) [32]  |  [31] QNX tid (32 bits) [0] |

In order to retrieve the QNX --> qnx-tid = perfetto-tid & 0x00000000FFFFFFFF

## Mapping QNX Thread Death to Perfetto

The QNX system reports a sequence of events during thread death and clean up
that can be tricky to interpret. This sequence is due to the operating system's
transparent reporting of the thread state event when it is being used to perform
clean up. There are subtle differences in the sequence of thread death related
trace events on QNX 7.1 and 8.0.

In order to simplify the thread state machine for Perfetto, *traced_qnx_probes*
tracks the QNX thread death sequence so that it can report it more intuitively
as DEAD followed by DESTROYED.
