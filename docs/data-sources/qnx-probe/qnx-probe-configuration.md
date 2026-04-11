# Configuring traced_qnx_probes

In order to run a trace that includes data collected by ***traced_qnx_probes***
you will need to create a perfetto trace configuration file that includes a
configuration for the qnx kernel probe.

The perfetto system uses JSON formatted configuration files to specify how long
the trace should last, what probes should be included in the trace and what
configuration each probe should use.

## traced_qnx_probes Configuration

```JSON
# Enable qnx data sources
data_sources {
  config {
    name: "qnx.kernel"
    target_buffer: 0
    qnx_config {
      qnx_kernel_buffers: 32
      qnx_kernel_kbuffers: 64
      qnx_kernel_wide_events: true
      qnx_cache_pages: 4
      qnx_cache_max_pages: -1
      qnx_trace_buffer_init_bytes: 512
    }
  }
}
```

| Parameter                       | Description                          | Default Value |
|---------------------------------|--------------------------------------|---------------|
| qnx_kernel_buffers | The number of buffers that tracelog will init for the QNX trace.|  32 |
| qnx_kernel_kbuffers | The number of kernel buffers that tracelog will init for the QNX trace. | 64 |
| qnx_kernel_wide_events | Flag indicating whether the QNX kernel tracing should produce wide events which contain additional data or fast events which are most concise. In fast mode we lose the priority information with the benefits of having small events which can be processed faster. | true |
| qnx_cache_pages | The number of pages initialized by default the parser's page cache. Recommend setting this to the number of CPUs x 2. | 4 |
| qnx_cache_max_pages | The maximum pages the page cache should allocate (must be at least as big) as the qnx_cache_pages value. Using -1 will allow the cache to grow unbounded. The cache will prefer to re-use existing pages so growth will only happen when needed (when parser is not keeping up). | -1 |
| qnx_trace_buffer_init_bytes | The initial size of the buffer used to hold the trace header values this dynamic buffer will grow as needed but reallocs can be avoided by selecting an initial size large enough to hold all the initial header data. | 512 |

You can find a working example of a perfetto configuration at
[qnx_probes.cfg](../../../test/configs/qnx_probes.cfg)

In a hypervised deployment with host and guest probes the configuration includes
both host and guest probes (e.g., Android and/or Linux). For details describing
how to configure the Linux probe see
[traced_probes](../../../docs/reference/traced_probes.md).

## Running a Perfetto Trace on QNX

Once you have configured your trace and included the ***traced_qnx_probes***
data source configuration in your JSON configuration file, you will want to run
a trace. See [Running a Perfetto Trace on QNX](qnx-probe-tracing.md) for further
details.
