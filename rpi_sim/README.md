# GenAI TCP simulation

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

Run from the ns-3 root:

```bash
./ns3 run "rpi-sim --config=rpi_sim/config/text-to-image.json"
./ns3 run "rpi-sim --config=rpi_sim/config/image-to-text.json"
./ns3 run "rpi-sim --config=rpi_sim/config/text-to-text.json"
```

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
