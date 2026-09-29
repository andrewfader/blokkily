# LLM composition assistant

A producer describes what they want in musical words — "a minor pentatonic
ascending", "offbeat hats", "make this jazzier" — and the assistant has a
language model write it into the open pattern. The model is shown the
session it is writing for (tuning, scale, tempo, meter, what is already
there), its answer is parsed and clamped like any other input, and nothing
reaches the song until the producer clicks Apply. One generation is one
undo step.

The assistant is not part of the audio path: backends answer on the GUI
thread, parse on the GUI thread, and Apply writes through the same
checkpoint the editors use. The audio thread never sees an LLM call.

## Where it lives

```
src/llm/
  llm_backend.{hpp,cpp}     -- the QObject seam every backend answers through,
                               the registry, and the shared network test seam
  trigger_json.{hpp,cpp}    -- the JSON wire dialect (mode, triggers)
  system_prompt.{hpp,cpp}   -- the standing instructions the model is given
  ollama_backend.cpp        -- local Ollama (default), no key
  gemini_backend.cpp        -- Google Gemini, BLOKKILY_GEMINI_KEY
  openai_compatible_backend.cpp
                            -- MiniMax, ChatGPT and OpenRouter: one class,
                               three vendor descriptions
  scripted_backend.{hpp,cpp}-- a script-driven backend for the gate
src/app/
  llm_model.{hpp,cpp}       -- QML-facing bridge: ask, proposal, apply, discard
  verify/scenario_llm.cpp   -- the bdd_llm_assistant gate
ui/
  PromptBar.qml             -- the bar in the editor column
features/
  llm_assistant.feature     -- the scenarios the unit and gate tests check
tests/
  llm_assistant_tests.cpp   -- wire-format and scripted backend cases
  fixtures/llm_script.jsonl -- the scripted backend's prompts and replies
```

## Wire format

The model answers in JSON; `trigger_json.cpp` is the only file that reads
or writes the format.

```
{
  "mode": "replace" | "add" | "modify",
  "triggers": [
    {
      "step": 0,                // sixteenth index, 0-based
      "length_steps": 1,        // may be fractional
      "note": {"key": 60, "velocity": 0.8, "cents": 0},
      "chord": {"root": 60, "intervals": [0,4,7], "velocity": 0.75,
                "inversion": 0, "strum_steps": 0,
                "velocities": [0.8,0.7,0.7]},
      "probability": 1.0,
      "ratchets": 1,
      "micro_offset": 0,
      "play_on_loop": 0
    }
  ]
}
```

The parser tolerates markdown fences and prose padding around the object,
clamps out-of-range fields (probability, ratchets, key, velocity, length),
drops triggers past the pattern's end, and admits every repair in
`corrections[]` so the bar can say what was repaired in plain language.

## Backends

| picker name  | endpoint (override)                                        | key variable              | default model        |
|--------------|------------------------------------------------------------|---------------------------|----------------------|
| `ollama`     | `http://localhost:11434` (`BLOKKILY_OLLAMA_URL`)           | none                      | `llama3.1`           |
| `gemini`     | `generativelanguage.googleapis.com`                        | `BLOKKILY_GEMINI_KEY`     | `gemini-2.5-flash`   |
| `minimax`    | `https://api.minimax.io` (`BLOKKILY_MINIMAX_URL`)          | `BLOKKILY_MINIMAX_KEY`    | `MiniMax-M3`         |
| `chatgpt`    | `https://api.openai.com` (`BLOKKILY_OPENAI_URL`)           | `BLOKKILY_OPENAI_KEY`     | `gpt-4o-mini`        |
| `openrouter` | `https://openrouter.ai/api` (`BLOKKILY_OPENROUTER_URL`)    | `BLOKKILY_OPENROUTER_KEY` | `openai/gpt-4o-mini` |

Every model is overridable with `BLOKKILY_<NAME>_MODEL` (`BLOKKILY_OPENAI_MODEL`
for ChatGPT). `BLOKKILY_LLM_BACKEND` picks the backend the bar opens on;
without it the bar opens on the local Ollama, so nothing leaves the machine
unless the producer chooses a cloud backend.

MiniMax, ChatGPT and OpenRouter speak the OpenAI chat-completions dialect at
`<url>/v1/chat/completions` with a Bearer key and force
`response_format: {type: "json_object"}` so the wire dialect survives
untouched. Gemini receives its key in the `x-goog-api-key` header, never in
the URL. A cloud backend without its key stays in the picker and answers
with an error naming the variable to set. When a provider refuses a request
(bad key, no credit, unknown model) its own message is shown, not the HTTP
status.

`scripted` is not in the public picker; the verify driver uses it via
`LlmModel::setBackendForTesting` to drive the gate without a network.

## Prompt bar UX

The bar sits in the editor column above the arrangement view. It has
three observable states:

- **Idle** — border is the line color, "Ready — <backend>" is muted,
  Apply and Discard are hidden, the field placeholder hints at the
  vocabulary.
- **Busy** — the input is locked, a spinner shows, the status reads
  "Asking <backend>…".
- **Proposal** — the border goes acid green, the status reads
  "Proposed: N triggers to <replace|add to|modify> the pattern" with any
  corrections appended, Apply and Discard appear, the field is locked with
  "Apply or discard the proposal below." as its placeholder.

Ctrl+K focuses the bar from anywhere; Escape inside the bar hands the
keyboard back to the editor.

## Tests

- `blokkily_llm_tests`: the wire format, the scripted backend and the system
  prompt, plus every live backend's request shape, reply parsing, missing
  key, transport failure, provider error (an HTTP 401 carrying a JSON
  body), and URL/model overrides — each backend against a capturing
  network access manager. Run with `ctest -L llm`.
- `bdd_llm_assistant`: drives the rendered bar through switching backend
  mid-request (the bar must unlock) and with a proposal held (it must be
  dropped), then prompt → proposal → discard → apply → undo → redo → add →
  apply, and saves three screenshots: idle, proposal, and end-of-scenario.

The network is never a test dependency.
