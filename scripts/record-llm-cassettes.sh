#!/usr/bin/env bash
# Records the live LLM gate against real providers: for each backend named,
# runs `blokkily --verify --scenario llm_live` with the provider answering for
# real, and keeps every exchange in tests/fixtures/llm_cassettes/<backend>.jsonl
# (keys scrubbed). Configure and build again afterwards: each cassette becomes
# a bdd_llm_live_<backend> gate that replays it with no key and no network.
#
#   scripts/record-llm-cassettes.sh minimax [chatgpt openrouter gemini ollama]
#
# Needs the backend's key in the environment (BLOKKILY_MINIMAX_KEY and so on,
# docs/llm-assistant.md) and a build in build/. The URL and model variables are
# cleared so the recording matches the defaults the gate replays. A run that
# does not end in "BDD PASS" keeps its cassette as <backend>.failed.jsonl, for
# reading, and leaves the committed one alone.
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
build=${BLOKKILY_BUILD_DIR:-$root/build}
tapes=$root/tests/fixtures/llm_cassettes
mkdir -p "$tapes" "$build/artifacts"
[[ $# -gt 0 ]] || { sed -n '2,15p' "$0"; exit 2; }

status=0
for backend in "$@"; do
    vendor=${backend^^}
    [[ $backend == chatgpt ]] && vendor=OPENAI
    key_var=BLOKKILY_${vendor}_KEY
    if [[ $backend != ollama && -z ${!key_var:-} ]]; then
        echo "$backend: $key_var is not set; skipped" >&2
        status=1
        continue
    fi
    tape=$tapes/$backend.recording.jsonl
    rm -f "$tape"
    echo "== recording $backend"
    set +e
    systemd-run --user --scope -q -p MemoryMax=4G -p CPUQuota=200% \
        env -u "BLOKKILY_${vendor}_URL" -u "BLOKKILY_${vendor}_MODEL" \
        QT_QPA_PLATFORM=offscreen QSG_RHI_BACKEND=software \
        ALSA_CONFIG_PATH=/dev/null DISPLAY=/nonexistent/blokkily-no-x11:0 WAYLAND_DISPLAY= \
        BLOKKILY_LLM_BACKEND="$backend" BLOKKILY_LLM_CASSETTE="$tape" \
        BLOKKILY_LLM_CASSETTE_MODE=record \
        nice -n 15 "$build/blokkily" --verify --scenario llm_live \
            --clap-fixture "$build/clap-fixtures/blokkily-test.clap" \
            --screenshot "$build/artifacts/llm-live-$backend-recorded.png" |
        tee "$build/artifacts/llm-live-$backend-recorded.log"
    result=${PIPESTATUS[0]}
    set -e
    if [[ $result -eq 0 ]] && grep -q '^BDD PASS' "$build/artifacts/llm-live-$backend-recorded.log"; then
        mv "$tape" "$tapes/$backend.jsonl"
        echo "== $backend recorded to tests/fixtures/llm_cassettes/$backend.jsonl"
    else
        [[ -f $tape ]] && mv "$tape" "$tapes/$backend.failed.jsonl"
        echo "== $backend failed (exit $result); see tests/fixtures/llm_cassettes/$backend.failed.jsonl" >&2
        status=1
    fi
done
exit $status
