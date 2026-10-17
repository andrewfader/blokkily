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

## As built

| Piece | Files | Role |
|---|---|---|
| `EventBus` | `guide/event_bus.hpp/.cpp` (Qt-free) | Append-only feed of `AppEvent`s. `AppController` and `PatternModel` publish every mutating command with who is acting: `user`, unless the dispatcher (`guide`) or `McpTools::call` (`mcp`) has set the source for the length of what it does and put it back afterwards. |
| `Narrator` | `guide/narrator.hpp/.cpp` (Qt-free) | Rule-based why-explanations and theory Q&A, grounded in a `SongSnapshot`. Steps are named as the grid numbers them, from 1. |
| `ActionDispatcher` | `guide/action_dispatcher.hpp/.cpp` (Qt-free, `ICommandSink`) | The guide's actions through the same methods the window's controls call. Keys off the keyboard are refused; the guide writes only onto empty steps; clearing the pattern asks first and is one step of undo. |
| `LessonEngine` | `guide/lesson.hpp/.cpp` (Qt-free) | JSON lessons under `ui/guide/lessons/`. A step's `check` names the event that completes it and its `count` how many times; only the producer's own edits count. |
| `LlmClient` | `guide/llm_client.hpp/.cpp` (Qt-free, injected transport) | The guide's own OpenAI-compatible model, off until configured; the key travels only over https or to this machine. |
| `GuideController` | `src/guide/guide_controller.hpp/.cpp` (Qt) | Bus to narrator to bubble (narration settles 150 ms, and stays quiet while a lesson's words are on screen), Q&A, lessons, actions. Settings live in QSettings, except in a `--verify` run, which keeps them in memory. |
| `GuideOverlay.qml` | `ui/guide/GuideOverlay.qml` | Mascot, bubble, spotlight ring, Q&A box, lesson and confirm controls. Idle, it draws nothing that takes a click. |

The guide is off until the producer turns it on with the GUIDE chip beside
HELP; GUIDE opens its bubble, and the bubble turns it off. A question its rules
cannot answer reaches a model only through the guide's own configured model,
or the prompt bar's model once the producer ticks ASK MODEL - that model may
be a cloud service, so it is never asked unasked. A model's answer that
arrives after another question, a timeout (30 s) or a change of backend is
dropped.

## Architecture

```
user -> AppController/PatternModel -> SongModel -> engine
              | (additive one-line emits)
         EventBus (append-only feed of AppEvents, source=user|mcp|guide|engine)
              |
     GuideController (Qt bridge)
        |  narrate: Narrator.describe(event, SongSnapshot) -> bubble
        |  ask:     Narrator.answer_question, else a model (opt-in)
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
  and `guide-overlay-lesson.png` inspected.
