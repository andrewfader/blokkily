// features/llm_assistant.feature, run on its own by the bdd_llm_assistant
// gate (`--scenario llm`). The producer's path is driven through the rendered
// window: the prompt bar is clicked into, a prompt is typed letter by letter,
// Return sends it, and the scripted backend (BLOKKILY_LLM_SCRIPT, no server,
// no network) answers with bytes the gate controls. The proposal must wait
// for Apply; Apply must write the scripted triggers into the canonical
// pattern, where the rendered editors show them; one undo must take the
// whole generation back; and Discard must drop a proposal without writing.
//
// Three screenshots are written so a person (or agent) can look at the bar
// in every phase the producer sees it: idle, holding a proposal, after apply.

#include "verify/harness.hpp"

#include "../llm/scripted_backend.hpp"

#include <QQuickItem>

#include <sstream>
#include <string>
#include <variant>

namespace blokkily::verify {
namespace {

void type_text(VerifyContext& ctx, const QString& text) {
    for (const auto ch : text) {
        if (ch == QLatin1Char(' ')) {
            ctx.type_key(Qt::Key_Space, QStringLiteral(" "));
        } else {
            const auto upper = ch.toUpper();
            ctx.type_key(static_cast<Qt::Key>(upper.unicode()), QString(ch));
        }
    }
}

std::size_t trigger_count(const SongModel& song) {
    return song.editPattern().events().size();
}

bool has_note(const SongModel& song, int step, int key) {
    for (const auto& trigger : song.editPattern().events()) {
        if (trigger.start / PatternModel::ticks_per_step != step) continue;
        if (std::holds_alternative<blokkily::Note>(trigger.musical_data) &&
            std::get<blokkily::Note>(trigger.musical_data).key == key)
            return true;
    }
    return false;
}

// Saves the rendered window to a named sibling of the gate's main artifact.
// The gate's `--screenshot` is the canonical end-of-scenario frame; these
// extras prove the bar looks right at every state without re-running the
// scenario.
void save_phase(VerifyContext& ctx, const QString& suffix) {
    const auto base = ctx.parser.value("screenshot");
    if (base.isEmpty()) return;
    const auto dot = base.lastIndexOf('.');
    const auto stem = (dot < 0) ? base : base.left(dot);
    const auto ext = (dot < 0) ? QString() : base.mid(dot);
    Q_UNUSED(ctx.save_screenshot_to(stem + "-" + suffix + ext));
}

void run_llm(VerifyContext& ctx) {
    std::stringstream details;
    const auto check = [&ctx](bool ok) { ctx.check(ok); };

    auto* bar = ctx.named("promptBar");
    auto* field = ctx.named("promptField");
    auto* apply = ctx.named("promptApply");
    auto* discard = ctx.named("promptDiscard");
    auto* status = ctx.named("promptStatus");
    auto* picker = ctx.named("backendPicker");
    check(bar != nullptr && field != nullptr && apply != nullptr &&
          discard != nullptr && status != nullptr && picker != nullptr);
    check(VerifyContext::usable(bar, 400, 30));
    check(VerifyContext::usable(field, 200, 22));
    check(VerifyContext::usable(apply, 60, 22));
    check(VerifyContext::usable(discard, 60, 22));
    ctx.reached("prompt bar rendered and usable");
    if (bar == nullptr || field == nullptr || apply == nullptr ||
        discard == nullptr || status == nullptr || picker == nullptr) {
        ctx.finish(details.str());
        return;
    }

    // Idle phase: the bar must show the backend name and a Ready status, no
    // proposal, and no Apply/Discard buttons.
    check(!ctx.llm.hasProposal());
    check(!apply->property("visible").toBool());
    check(!discard->property("visible").toBool());
    check(ctx.llm.statusText().contains(QStringLiteral("Ready")));
    check(ctx.llm.backendName().contains(QStringLiteral("Scripted")));
    save_phase(ctx, QStringLiteral("idle"));
    ctx.reached("idle: ready, no proposal");

    // Switching the backend must drop any in-flight answer and re-name the
    // status, so a producer who changes backends never sees a stale proposal
    // answered by a backend they have already moved on from. The scripted
    // backend lives behind setBackendForTesting, so the public switch
    // exercise uses the two named backends.
    ctx.llm.setBackend(QStringLiteral("ollama"));
    check(ctx.llm.backendKey() == QStringLiteral("ollama"));
    check(ctx.llm.statusText().contains(QStringLiteral("Ready")));
    check(ctx.llm.backendName().contains(QStringLiteral("Ollama")));
    ctx.llm.setBackend(QStringLiteral("gemini"));
    check(ctx.llm.backendKey() == QStringLiteral("gemini"));
    check(ctx.llm.statusText().contains(QStringLiteral("Ready")));
    check(ctx.llm.backendName().contains(QStringLiteral("Gemini")));
    // Leave the scripted backend in place for the rest of the scenario:
    // the gate's script is what answers the prompts, and switching away
    // would make every reply come back as an error.
    ctx.llm.setBackendForTesting(std::make_unique<blokkily::llm::ScriptedBackend>());
    check(ctx.llm.backendKey() == QStringLiteral("scripted"));
    ctx.reached("backend switch is reflected in the bar");

    // The session opens on the default VERSE pattern: four drum triggers.
    const auto before = trigger_count(ctx.song);
    check(before == 4);

    // A prompt typed into the bar is answered with a proposal — which must
    // not touch the pattern before Apply.
    ctx.click_at(field, {field->width() / 2, field->height() / 2}, Qt::LeftButton);
    ctx.settle(50);
    type_text(ctx, QStringLiteral("pentatonic"));
    ctx.type_key(Qt::Key_Return, QStringLiteral("\r"));
    ctx.reached("prompt typed and sent");
    // The scripted backend answers from the event loop; give it room.
    for (int i = 0; i < 40 && !ctx.llm.hasProposal(); ++i) ctx.settle(50);
    check(ctx.llm.hasProposal());
    check(ctx.llm.proposalCount() == 5);
    check(trigger_count(ctx.song) == before); // preview, not a write
    check(apply->property("visible").toBool());
    check(discard->property("visible").toBool());
    check(status->property("text").toString().contains(QStringLiteral("Proposed")));
    save_phase(ctx, QStringLiteral("proposal"));
    ctx.reached("reply proposed, pattern untouched");

    // Discard drops the proposal without touching the pattern. The bar
    // returns to its idle look and the pattern keeps the four drum triggers.
    ctx.click_at(discard, {discard->width() / 2, discard->height() / 2},
                 Qt::LeftButton);
    ctx.settle(50);
    check(!ctx.llm.hasProposal());
    check(trigger_count(ctx.song) == before);
    check(!apply->property("visible").toBool());
    ctx.reached("discard drops the proposal");

    // Ask again — this is the path the producer takes. Apply writes the
    // scripted five-note replace through the rendered button; the editors
    // read the same canonical pattern.
    ctx.click_at(field, {field->width() / 2, field->height() / 2}, Qt::LeftButton);
    ctx.settle(50);
    type_text(ctx, QStringLiteral("pentatonic"));
    ctx.type_key(Qt::Key_Return, QStringLiteral("\r"));
    for (int i = 0; i < 40 && !ctx.llm.hasProposal(); ++i) ctx.settle(50);
    check(ctx.llm.hasProposal());
    ctx.click_at(apply, {apply->width() / 2, apply->height() / 2}, Qt::LeftButton);
    ctx.settle(80);
    check(trigger_count(ctx.song) == 5);
    check(has_note(ctx.song, 0, 57) && has_note(ctx.song, 2, 60) &&
          has_note(ctx.song, 4, 62) && has_note(ctx.song, 6, 64) &&
          has_note(ctx.song, 8, 67));
    check(ctx.rendered_step(0) && ctx.rendered_step(2) && ctx.rendered_step(8));
    ctx.reached("proposal applied, editors follow");

    // One undo takes the whole generation back: the verse's drums return.
    check(ctx.song.undo());
    ctx.settle(50);
    check(trigger_count(ctx.song) == before);
    check(has_note(ctx.song, 0, 36));
    ctx.reached("one undo restores the pattern");

    // Redo puts the generation back, then an "add" prompt layers on top of
    // it: the hats join the pentatonic rather than replacing it.
    check(ctx.song.redo());
    ctx.settle(50);
    check(trigger_count(ctx.song) == 5);
    ctx.click_at(field, {field->width() / 2, field->height() / 2}, Qt::LeftButton);
    ctx.settle(50);
    type_text(ctx, QStringLiteral("offbeat hats"));
    ctx.type_key(Qt::Key_Return, QStringLiteral("\r"));
    for (int i = 0; i < 40 && !ctx.llm.hasProposal(); ++i) ctx.settle(50);
    check(ctx.llm.hasProposal() && ctx.llm.proposalCount() == 4);
    ctx.click_at(apply, {apply->width() / 2, apply->height() / 2}, Qt::LeftButton);
    ctx.settle(80);
    check(trigger_count(ctx.song) == 9);
    check(has_note(ctx.song, 0, 57) && has_note(ctx.song, 14, 42));
    ctx.reached("add mode layers onto the pattern");

    details << "prompt -> proposal -> discard -> apply -> undo -> redo -> add, "
            << trigger_count(ctx.song) << " triggers on the pattern";
    check(ctx.save_screenshot());
    ctx.finish(details.str());
}

[[maybe_unused]] const bool registered = register_scenario("llm", run_llm);

} // namespace
} // namespace blokkily::verify
