# GenAI TCP simulation

## Getting started

The `rpi_sim` application lives inside the ns-3 tree and is built as part of ns-3. Build
ns-3 from scratch once, then run any of the configurations below.

### Prerequisites

A working C++ toolchain plus the tools ns-3 needs to configure and build:

```bash
# Debian / Ubuntu
sudo apt update
sudo apt install -y g++ python3 cmake make git
```

### 1. Clone the repository

```bash
git clone git@github.com:wayslab/ns-3-dev.git
cd ns-3-dev
```

The `rpi_sim` sources are wired into the build from the ns-3 root `CMakeLists.txt`
(`add_subdirectory(rpi_sim)`), so no extra setup is required to pick them up.

### 2. Configure ns-3

Run the configure step once from the ns-3 root. This generates the build system and
detects available dependencies:

```bash
./ns3 configure --enable-examples --enable-tests
```

Use `./ns3 configure -d optimized` for a faster, non-debug build, or
`-d debug` (the default) while developing.

### 3. Build

Build everything (this compiles ns-3 and the `rpi-sim` executable):

```bash
./ns3 build
```

To build only the rpi_sim target after the first full build:

```bash
./ns3 build rpi-sim
```

The compiled binary is placed alongside the sources at
`rpi_sim/ns3-dev-rpi-sim-default` (gitignored). All commands below are run from the
ns-3 root.

## Overview

This directory contains two custom ns-3 applications:

- `GenAIUser` samples a request size and sends one framed application message over TCP.
- `GenAIServer` reassembles the request, samples a processing delay, then samples and sends a response.

Both applications have a `Modality` attribute. The current supported values are `text` and
`image`.

The GenAIUser/client uses `10.0.0.2`; GenAIServer uses the visually distinct `10.0.0.200`.

Complete simulation configurations are in `config/`. Request sizes use modality-specific
log-normal distributions. The configured `mu` and `sigma` are the mean and standard
deviation of `ln(request_size_bytes)`. The text and image files each contain the matching
request-size fit and set `genai_user.modality` accordingly. Time to first response token is
sampled in seconds from a log-normal mixture selected by the user-to-server modality pair.
Response size is also selected by modality pair: text-to-text uses Pareto, while
image-to-text and text-to-image use log-normal distributions.

### Run a single simulation

Run from the ns-3 root:

```bash
./ns3 run "rpi-sim --config=rpi_sim/config/text-to-image.json"
./ns3 run "rpi-sim --config=rpi_sim/config/image-to-text.json"
./ns3 run "rpi-sim --config=rpi_sim/config/text-to-text.json"
```

### Generate a pcap dataset

`config/generate-pcaps.sh` runs each modality pair many times and collects one pcap per
run into a dataset directory. Run it from the ns-3 root after building:

```bash
# 200 runs per pair, captured from the client side (defaults)
./rpi_sim/config/generate-pcaps.sh

# Override the run count, capture viewpoint, and output directory
RUNS=500 CAPTURE_SIDE=server ./rpi_sim/config/generate-pcaps.sh /path/to/dataset
```

`RUNS` is the number of runs per pair, `CAPTURE_SIDE` is `client` or `server`, and the
optional positional argument is the output root (default
`rpi_sim/generated_pcap/dataset`). Set `OVERWRITE=1` to regenerate captures that already
exist. The script needs `python3` to write per-run configs.

`text-to-image.json`, `image-to-text.json`, and `text-to-text.json` name each simulation
using the user-to-server modality pair. With the text-to-image configuration, captures are
written as:

```text
rpi_sim/generated_pcap/text_image_pcap-0-0.pcap
rpi_sim/generated_pcap/text_image_pcap-1-0.pcap
```

A sampled application message is not one Ethernet packet. The applications pass the full
message to TCP, and TCP segments it according to its send buffer, congestion state, and the
configured 1448-byte segment size.


A finite log-normal mixture is also available through
`lognormalmixture/LogNormalMixtureRandomVariable`. Configure it with:

```json
{
  "distribution": "lognormal_mixture",
  "components": [
    {"weight": 0.7, "mu": 7.0, "sigma": 0.2},
    {"weight": 0.3, "mu": 9.0, "sigma": 0.4}
  ]
}
```

Weights must be positive and do not need to sum to one; the implementation normalizes them
when selecting a component.
