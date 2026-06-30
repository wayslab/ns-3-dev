#!/usr/bin/env bash
# Example from the ns-3 root: RUNS=200 CAPTURE_SIDE=client ./rpi_sim/config/generate-pcaps.sh
set -euo pipefail

# Resolve paths so the script works from any current directory.
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
RPI_SIM_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
NS3_DIR="$(cd -- "${RPI_SIM_DIR}/.." && pwd)"

# Environment variables may override the run count and capture viewpoint.
RUNS="${RUNS:-200}"
CAPTURE_SIDE="${CAPTURE_SIDE:-client}"
OUTPUT_ROOT="${1:-${RPI_SIM_DIR}/generated_pcap/dataset}"

if ! [[ "${RUNS}" =~ ^[1-9][0-9]*$ ]]; then
    printf 'RUNS must be a positive integer, got: %s\n' "${RUNS}" >&2
    exit 1
fi

# ns-3 names captures as <prefix>-<node>-<device>.pcap.
case "${CAPTURE_SIDE}" in
    client)
        pcap_suffix="0-0"
        ;;
    server)
        pcap_suffix="1-0"
        ;;
    *)
        printf 'CAPTURE_SIDE must be client or server, got: %s\n' "${CAPTURE_SIDE}" >&2
        exit 1
        ;;
esac

command -v python3 >/dev/null || {
    printf 'python3 is required\n' >&2
    exit 1
}

[[ -x "${NS3_DIR}/ns3" ]] || {
    printf 'ns-3 launcher not found: %s\n' "${NS3_DIR}/ns3" >&2
    exit 1
}

# Pair each configuration name with the pcap prefix produced by rpi-sim.
pairs=(
    "text-to-text:text_text_pcap"
    "text-to-image:text_image_pcap"
    "image-to-text:image_text_pcap"
)

# Keep intermediate configs and duplicate interface captures out of the dataset.
temp_root="$(mktemp -d)"
trap 'rm -rf -- "${temp_root}"' EXIT

mkdir -p -- "${OUTPUT_ROOT}"

for pair_entry in "${pairs[@]}"; do
    pair="${pair_entry%%:*}"
    pcap_prefix="${pair_entry##*:}"
    source_config="${SCRIPT_DIR}/${pair}.json"
    pair_output="${OUTPUT_ROOT}/${pair}"

    [[ -f "${source_config}" ]] || {
        printf 'Configuration not found: %s\n' "${source_config}" >&2
        exit 1
    }

    mkdir -p -- "${pair_output}"
    printf 'Generating %d %s pcaps for %s\n' "${RUNS}" "${CAPTURE_SIDE}" "${pair}"

    for ((run = 1; run <= RUNS; run++)); do
        run_label="$(printf '%03d' "${run}")"
        final_pcap="${pair_output}/${pair}-${run_label}.pcap"

        if [[ -f "${final_pcap}" && "${OVERWRITE:-0}" != "1" ]]; then
            printf '\r  %s: %d/%d (existing)' "${pair}" "${run}" "${RUNS}"
            continue
        fi

        run_dir="${temp_root}/${pair}-${run_label}"
        run_config="${run_dir}/config.json"
        run_pcap_dir="${run_dir}/pcap"
        mkdir -p -- "${run_pcap_dir}"

        # Create a reproducible per-run config without modifying the source JSON.
        python3 - "${source_config}" "${run_config}" "${run}" "${run_pcap_dir}" <<'PY'
import json
import sys
from pathlib import Path

source, destination, run, pcap_directory = sys.argv[1:]
config = json.loads(Path(source).read_text())
config["simulation"]["run"] = int(run)
config["simulation"]["verbose"] = False
config["simulation"]["pcap_directory"] = pcap_directory
Path(destination).write_text(json.dumps(config, indent=2) + "\n")
PY

        # Run from the ns-3 root because configuration paths are resolved there.
        (
            cd -- "${NS3_DIR}"
            ./ns3 run "rpi-sim --config=${run_config}" >/dev/null
        )

        # Retain one viewpoint so each simulation produces exactly one dataset pcap.
        generated_pcap="${run_pcap_dir}/${pcap_prefix}-${pcap_suffix}.pcap"
        if [[ ! -f "${generated_pcap}" ]]; then
            printf '\nExpected pcap was not generated: %s\n' "${generated_pcap}" >&2
            exit 1
        fi

        mv -f -- "${generated_pcap}" "${final_pcap}"
        rm -rf -- "${run_dir}"
        printf '\r  %s: %d/%d' "${pair}" "${run}" "${RUNS}"
    done

    printf '\n'
done

printf 'Generated dataset under %s\n' "${OUTPUT_ROOT}"
