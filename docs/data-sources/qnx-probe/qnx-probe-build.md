# Building Perfetto for QNX

The QNX Perfetto port is supported for QNX 7.1 and QNX 8.0. In order to build
for QNX you will need to have the corresponding QNX SDP installed on your
system. See [QNX SDP](http://www.qnx.com) for details on how to install the
QNX SDP for either 7.1 and/or 8.0.

The Perfetto port uses the same tools and scheme to build Perfetto as the main
Google project. See
[Perfetto Build Instructions](https://perfetto.dev/docs/contributing/build-instructions)
for a detailed description of the Perfetto build system.

## Table of Contents

- [Clone the Perfetto project](#clone-the-perfetto-project)
- [Create a Perfetto build configuration](#create-a-perfetto-build-configuration-for-qnx-targets)
- [Build Perfetto for QNX](#build-the-perfetto-components-for-qnx)
- [Deploying and Running Perfetto on QNX](#deploying-perfetto-on-qnx)

## Clone the Perfetto project

Once you have QNX SDP installed on your system you will need to get the
Perfetto source code from
[github/qnx-ports/perfetto](https://github.com/qnx-ports/perfetto). Specifically the ***qnx-main*** branch which contains
*traced_qnx_probe* and the QNX port of *traced*, *traced_relay* and *perfetto*.

**NOTE**: If you are building *traced_relay* and would like to leverage
virtio-vsock to communicate with *traced* you will need to have the QNX
Virtualization Framework package (e.g.,
***com.qnx.qnx800.target.qavf.virtual_socket***) installed in your SDP with the
correspond license.

```bash
# 1. Create a local directory for the perfetto project
mkdir perfetto 

# 2. Change into the newly created directory
cd perfetto

# 3. Clone the perfetto project locally
git clone git@github.com:qnx-ports/perfetto.git .

# 4. Checkout the qnx-main branch in order to get the QNX probe sources
git checkout qnx-main
```

## Create a Perfetto build configuration for QNX Targets

Once you have the git project you will need to install the build dependencies

```bash
tools/install-build-deps
```

After successful installation you will need to create a perfetto build arguments
file that describes the target you are building for:

```bash
# Run gn args which will open an editor for you to create the args file
# NOTE this also defines the output directory here you can see we are showing
#      the output for an x86_64 target but arm64 is also supported. Similarly, 
#      the out directory name includes the QNX version which is helpful if you
#      may be building for multiple versions of QNX. 
tools/gn args out/qnx8.0_x64
```

Edit your build arguments as follows:

```bash
# Ensure the args file has the following lines. NOTE target_cpu will be either
# "x64" or "arm64" depending on what type of system you are targeting.
target_os = "qnx"
target_cpu = "x64"

is_debug = false
```

**NOTE**: The QNX version 7.1 or 8.0 is not set in the build args, it is
determined by the SDP you have enabled in your build environment. Ensure you
source the appropriate *<QNX_SDP_DIR>/qnxsdp_env.sh* file that corresponds to
the SDP you which to use for your target system.

## Build the Perfetto Components for QNX

Not all the Perfetto components are supported on QNX and the QNX kernel probe
doesn't build by default. You will need to specify each of the components you
want to build for QNX as part of the build command.

```bash
# First you will need to source the QNX SDP environment so that your system
# knows where to find the QNX tooling (compiler, headers, libraries, ...)
source <QNX-SDP-directory>/qnxsdp-env.sh

# Change to the root directory of your Perfetto project
cd perfetto

# Build the Perfetto components for QNX including the QNX kernel probe
# NOTE: Here we are showing qnx8.0_x64 but you will need to specify the same
#       path that you specified to tools/gn args previously.
tools/ninja -C out/qnx8.0_x64 traced traced_relay traced_qnx_probe perfetto
```

This will leave the compiled executables for ***traced***, ***traced_relay***,
***traced_qnx_probes*** and ***perfetto*** in the out/qnx8.0_x64 directory. You
will need to copy these to your target in order to run them.

## Deploying Perfetto on QNX

Once you have a build of the Perfetto components for your target QNX system, you
will need to deploy them to the target in order to run a trace. For a detailed
description of how to deploy and run the Perfetto components on QNX see
[Deploying and Running Perfetto on QNX](qnx-probe-deployment.md).
