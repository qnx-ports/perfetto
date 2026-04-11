# Running a Perfetto Trace on QNX

This document describes how to run a Perfetto trace on QNX system. Before
running a trace make sure you have properly deployed the Perfetto components to
your trace target and configured your trace parameters. For details on deploying
and configuring see [Deploying Perfetto on QNX](qnx-probe-deployment.md) and
[Configuring QNX Probe](qnx-probe-configuration.md).

## Table of Contents

- [Configuring Perfetto Components](#configuring-perfetto-components)
  - [Configuration Variables](#configuration-variables)
- [Running traced](#running-traced)
- [Running traced_relay](#running-traced_relay)
- [Running traced_qnx_probes](#running-traced_qnx_probes)
- [Running perfetto](#running-perfetto)

## Configuring Perfetto Components

In order to run a trace the perfetto components must be configured so that they
can communicate with one another.  Specifically, probes/providers must know
the *"provider"* domain socket used to locate either
**traced***/***traced_relay*** (depending on your deployment).

Similarly, the ***perfetto*** client must know the *"consume"* domain socket
used to communicate locally with ***traced***.

Finally, ***traced_relay*** will need to be configured in order to communicate
with ***traced***. **traced** will need to know how and where to listen and
***traced_relay*** will need to know where to connect.

Relay communications can be configured using different protocols (e.g., TCP or
virtio-vsock) and the format of the address will change accordingly.

### Configuration Variables

In general, configuring the Perfetto components requires specification of the
following:

| Variable                     | Description | Format | Example |
|------------------------------|-------------|--------|---------|
| ***\<probe-relay-socket\>***       | Domain socket path used by probes to communicate locally with traced_relay. | PATH | /tmp/perfetto.probe |
| ***\<relay-traced-address\>***     | Address used by traced to listen for relay connections and by relay to connect to traced (This can be TCP or virtio-vsock). | \<ip-address\>:\<port\> OR \<vsock-channel-id\>:\<port\> | 192.168.1.79:21000 or 3:1234 |
| ***\<perfetto-traced-socket\>***   | Domain socket path used by perfetoo to communicate traced. | PATH | /tmp/perfetto.traced.sock |

## Running traced

***traced*** requires the following environment variables:

| Parameter                  | Variable Value             |
|----------------------------|----------------------------|
|PERFETTO_PRODUCER_SOCK_NAME | \<relay-traced-address\>   |
|PERFETTO_CONSUMER_SOCK_NAME | \<perfetto-traced-socket\> |

#### Example

```bash
export PERFETTO_PRODUCER_SOCK_NAME=<ip-address>:<port>
export PERFETTO_CONSUMER_SOCK_NAME=<perfetto-traced-socket>
```

In addition, ***traced*** requires that ***libperfetto.so*** be in
**LD_LIBRARY_PATH**. For example,

```bash
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/opt/perfetto/lib
```

#### Run traced

Once the environment variables are set you can run traced and specify --enable-relay-endpoint to tell it to listen for relay connections.

```bash
bin/traced --enable-relay-endpoint
```

## Running traced_relay

***traced_relay*** requires the following environment variables:

| Parameter                  | Variable Value           |
|----------------------------|--------------------------|
|PERFETTO_PRODUCER_SOCK_NAME | \<probe-relay-socket\>   |
|PERFETTO_RELAY_SOCK_NAME    | \<relay-traced-address\> |

#### Example

```bash
export PERFETTO_PRODUCER_SOCK_NAME=<probe-relay-socket>
export PERFETTO_RELAY_SOCK_NAME=<relay-traced-address>
```

#### Run traced_relay

```bash
bin/traced_relay
```

## Running traced_qnx_probes

***traced_qnx_probes*** requires the following environment variables:

| Parameter                  | Variable Value         |
|----------------------------|------------------------|
|PERFETTO_PRODUCER_SOCK_NAME | \<probe-relay-socket\> |

#### Example

```bash
export PERFETTO_PRODUCER_SOCK_NAME=<probe-relay-socket>
```

#### Run traced_qnx_probes

```bash
bin/traced_qnx_probes
```

## Running perfetto

Running ***perfetto*** will run the trace session based on your configuration.
*perfetto* requires the following environment variables:

| Parameter                  | Variable Value             |
|----------------------------|----------------------------|
|PERFETTO_CONSUMER_SOCK_NAME | \<perfetto-traced-socket\> |

#### Example

```bash
export PERFETTO_CONSUMER_SOCK_NAME=<perfetto-traced-socket>
```

### Command Line Parameters

| Parameter          | Description | Value | Example |
|--------------------|-------------|-------|---------|
| -o \<output-file\> | The path to the file in which you want to capture the Perfetto trace. | PATH | qnx8.0_x64.perfetto-trace |
| --txt | The format of the output file | --txt | --txt |
| -c \<config-file\> | The path to the trace configuration file | PATH | etc/qnx_probes.cfg |

#### Run perfetto

```bash
bin/perfetto -o qnx8.0_x64.ptrace --txt -c etc/qnx_probes.cfg
```
