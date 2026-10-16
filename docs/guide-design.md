# Floating guide design (feature/staff-notation) — reconciled with upstream

## What already exists upstream (origin/main, releases 0.1.21→0.3.0)

Andrew is right: much of this was built upstream while this branch sat still.
Do NOT duplicate any of it:

- **LLM composition assistant** (`docs/llm-assistant.md`, `src/llm/`,
  `src/app/llm_model.*`, `ui/PromptBar.qml`): Ollama (local default), Gemini,
  MiniMax, ChatGPT, OpenRouter backends; scripted backend + HTTP cassettes for
  tests; JSON wire dialect for writing notes; proposal → Apply writes through
  the canonical pattern as one undo step. This is the "LLM help" seam.
- **MCP server** (`docs/mcp.md`, `src/app/mcp_{bridge,protocol,server,tools}.*`,
  `blokkily --mcp`): `get_session`, `write_notes`, `transport`,
  `set_instrument`, `describe`/`call`/`get_property`, `screenshot`, ... —
  tools write through the same undoable paths as the window's controls.
  This is the "MCP acts" seam.
- **Tutorial.qml** (`ui/Tutorial.qml`, `features/tutorial.feature`,
  `cmake/feature_tutorial.cmake`): Blokkily the mascot (pixel-art T-tetromino
  broccoli), HELP-button tour with speech bubble, flying ring, keycap
  tooltips, never blocks the window. This is the "floating character" seam.

## What Andrew's ask adds on top

1. **Continuous narration** — nobody narrates user/MCP actions as they happen
   with *why* explanations. The tutorial is a one-shot scripted tour; the
   prompt bar only acts when asked.
2. **An always-on guide** — the mascot exists but only during the tour. The
   ask wants it floating, watching, narrating, teachable.
3. **Interactive lessons** — tutorial steps are hardcoded in QML; there is no
   lesson format, no checks, no guide-performed actions.
4. **Theory Q&A** — the prompt bar generates music; nothing answers "why does
   this chord sound sad?" (rule-based or via the existing backends).
5. **In-process guide actions** — MCP acts out-of-process; the on-screen guide
   needs an in-process dispatcher calling the same command functions.

## What this branch implements (gap pieces only)

New, self-contained, under `src/guide/`, `include/blokkily/guide/`,
`ui/guide/` — designed to dock into the upstream seams on rebase:

| Piece | Files | Docks into (on rebase) |
|---|---|---|
| `EventBus` | `guide/event_bus.hpp/.cpp` (Qt-free) | AppController/PatternModel emit; upstream `McpTools::call` gets one emit line per tool |
| `Narrator` | `guide/narrator.hpp/.cpp` (Qt-free) | Rule-based why-explanations + theory Q&A; upstream LLM backends stay the upgrade path |
| `ActionDispatcher` | `guide/action_dispatcher.hpp/.cpp` (Qt-free, `ICommandSink`) | Sink adapter calls the same Q_INVOKABLEs QML calls; same functions upstream's `describe`/`call` tools reach |
| `LessonEngine` | `guide/lesson.hpp/.cpp` (Qt-free, JSON lessons) | Lessons as data under `ui/guide/lessons/`; drives the overlay's tour UI |
| `LlmClient` | `guide/llm_client.hpp/.cpp` (Qt-free, injected transport) | Standalone OpenAI-compatible client, off by default; on rebase can delegate to upstream's backend registry |
| `GuideController` | `src/guide/guide_controller.hpp/.cpp` (Qt) | QObject bridge: bus → narrator → bubble; Q&A; lessons; settings via QSettings |
| `GuideOverlay.qml` | `ui/guide/GuideOverlay.qml` | Floating mascot + bubble + spotlight ring + tour; converges with upstream `Tutorial.qml` on rebase (same concepts, same objectName targets) |
| Lessons | `ui/guide/lessons/*.json` | v1: first beat, triads, bass lines, lead sheet, call & response |

Additive edits only to existing files: `EventBus` member + one-line emits in
`AppController`/`PatternModel` mutating commands; `GuideOverlay` mount in
`ui/Main.qml`; guide toggle `Chip` in `ui/TransportBar.qml`; `guideController`
context property in `gui_main.cpp`; sources/tests in `CMakeLists.txt`;
`--scenario guide` in the verify driver; `features/guide.feature`.

## Deliberately left out

- Touching upstream's `Tutorial.qml`, `src/llm/*`, `src/app/mcp_*`: they are
  not in this tree. The overlay is written so the rebase is a convergence,
  not a rewrite (same mascot/bubble/ring vocabulary, same spotlight
  objectNames).
- Wiring Q&A into upstream's `LlmBackend` registry: not in this tree. The
  standalone client speaks the same OpenAI chat-completions dialect the
  upstream backends use, so the rebase just swaps the transport.
- `McpTools::call` emission hooks: one line per tool, documented here —
  `bus.emit({EventSource::mcp, kind_for(tool), ...})` at the top of
  `McpTools::call`, mapping tool names to `EventKind`.
- Voice output (non-goal, unchanged).

## Architecture (this branch)

```
user -> AppController/PatternModel -> SongModel -> engine
              | (additive one-line emits)
         EventBus (append-only feed of AppEvents, source=user|mcp|guide|engine)
              |
     GuideController (Qt bridge)
        |  narrate: Narrator.describe(event, SongSnapshot) -> bubble
        |  ask:     Narrator.answer_question, else LlmClient (opt-in)
        |  teach:   LessonEngine (JSON) -> overlay tour/spotlight/checks
        |  act:     ActionDispatcher -> ICommandSink -> AppController/PatternModel
        v
     GuideOverlay.qml (mascot, bubble, spotlight ring, Q&A box, confirm)
```

Every guide action emits an `AppEvent` with `source=guide`, so narration stays
truthful ("I moved that note up a fifth — hear the difference?"). Destructive
actions (clear pattern, …) require in-bubble confirmation first.

## Build & test (repo AGENTS.md applies)

- Guide core joins `blokkily_core` (Qt-free); `GuideController` joins the
  `blokkily` GUI target.
- `tests/guide_tests.cpp` → `blokkily_guide_tests` (labels `unit;guide`):
  bus, narrator grounding, dispatcher + fake sink, lesson engine, llm client
  with fake transport (no network, no key).
- `features/guide.feature` + `--scenario guide` gate (`bdd_guide`,
  labels `bdd;e2e;integration;screenshot;guide`) with `guide-overlay.png`
  inspected.
