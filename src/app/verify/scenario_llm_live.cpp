// features/llm_assistant.feature, "A live model's answer reaches the pattern":
// the scenario `--scenario llm_live` runs. Where `--scenario llm` proves the
// bar against a script, this one proves a real backend — the request it
// builds, the provider's answer, the parser, Apply and undo — by driving the
// rendered prompt bar against whichever live backend BLOKKILY_LLM_BACKEND
// names.
//
// It runs two ways (docs/llm-assistant.md):
//   record — BLOKKILY_LLM_CASSETTE_MODE=record calls the provider for real
//            and keeps the exchange in the cassette BLOKKILY_LLM_CASSETTE
//            names (scripts/record-llm-cassettes.sh);
//   replay — the bdd_llm_live_<backend> gates answer the same requests from
//            the committed cassette, with no key and no network.
// A model chooses its own notes, so the checks are about the path, not the
// music: an answer arrives, it parses into triggers inside the pattern,
// nothing is written before Apply, Apply writes exactly the proposal, one
// undo takes it back, and a second line carries the first as history.

#include "verify/harness.hpp"

#include <QQuickItem>

#include <iostream>
#include <sstream>
#include <string>

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

bool inside_pattern(const SongModel& song, int steps) {
    for (const auto& trigger : song.editPattern().events())
        if (trigger.start < 0 || trigger.start / PatternModel::ticks_per_step >= steps)
            return false;
    return true;
}

void save_phase(VerifyContext& ctx, const QString& suffix) {
    const auto base = ctx.parser.value("screenshot");
    if (base.isEmpty()) return;
    const auto dot = base.lastIndexOf('.');
    const auto stem = (dot < 0) ? base : base.left(dot);
    const auto ext = (dot < 0) ? QString() : base.mid(dot);
    Q_UNUSED(ctx.save_screenshot_to(stem + "-" + suffix + ext));
}

// Types `prompt` into the rendered bar, sends it, and waits for the answer.
// A live provider is given BLOKKILY_LLM_WAIT_MS (default three minutes); a
// cassette answers on the next turn of the event loop.
bool ask_through_bar(VerifyContext& ctx, QQuickItem* field, const QString& prompt) {
    ctx.click_at(field, {field->width() / 2, field->height() / 2}, Qt::LeftButton);
    VerifyContext::settle(50);
    type_text(ctx, prompt);
    ctx.type_key(Qt::Key_Return, QStringLiteral("\r"));
    VerifyContext::settle(20);
    if (!ctx.llm.busy() && !ctx.llm.hasProposal()) return false; // never sent
    bool ok = false;
    const int wait_ms = qEnvironmentVariableIntValue("BLOKKILY_LLM_WAIT_MS", &ok);
    const int budget = ok && wait_ms > 0 ? wait_ms : 180000;
    for (int waited = 0; waited < budget && ctx.llm.busy(); waited += 100)
        VerifyContext::settle(100);
    return ctx.llm.hasProposal();
}

void run_llm_live(VerifyContext& ctx) {
    std::stringstream details;
    const auto check = [&ctx](bool ok) { ctx.check(ok); };
    // Every status the bar shows is printed, so a failing live run says what
    // the provider answered rather than only that it failed.
    QObject::connect(&ctx.llm, &LlmModel::statusChanged, &ctx.llm, [&ctx] {
        std::cout << "llm status: " << ctx.llm.statusText().toStdString() << '\n';
    });

    auto* field = ctx.named("promptField");
    auto* apply = ctx.named("promptApply");
    check(field != nullptr && apply != nullptr);
    if (field == nullptr || apply == nullptr) {
        ctx.finish("the prompt bar is missing");
        return;
    }

    // The live backend, not the gate's script: the one the run names.
    const auto backend = qEnvironmentVariable("BLOKKILY_LLM_BACKEND");
    check(!backend.isEmpty() && ctx.llm.backendNames().contains(backend));
    ctx.llm.setBackend(backend);
    check(ctx.llm.backendKey() == backend);
    check(ctx.llm.statusText().startsWith(QStringLiteral("Ready")));
    ctx.reached("the live backend is chosen");

    // A replace: the producer's line is answered with a proposal that waits
    // for Apply, and every trigger lands inside the pattern.
    const auto before = trigger_count(ctx.song);
    const int steps = ctx.pattern.stepCount();
    check(ask_through_bar(ctx, field, QStringLiteral("a minor pentatonic melody ascending")));
    const int proposed = ctx.llm.proposalCount();
    check(proposed > 0);
    check(trigger_count(ctx.song) == before); // a preview, not a write
    check(apply->property("visible").toBool());
    details << "first answer: " << ctx.llm.proposalSummary().toStdString() << "; ";
    save_phase(ctx, QStringLiteral("proposal"));
    ctx.reached("a live answer is proposed, pattern untouched");

    const bool adds = ctx.llm.proposalSummary().contains(QStringLiteral("to add to"));
    ctx.click_at(apply, {apply->width() / 2, apply->height() / 2}, Qt::LeftButton);
    VerifyContext::settle(80);
    const auto applied = trigger_count(ctx.song);
    check(applied == (adds ? before : 0) + static_cast<std::size_t>(proposed));
    check(inside_pattern(ctx.song, steps));
    const auto& written = ctx.song.editPattern().events();
    check(!written.empty() &&
          ctx.rendered_step(static_cast<int>(written.front().start /
                                             PatternModel::ticks_per_step)));
    ctx.reached("Apply writes the live proposal and the editors show it");

    check(ctx.song.undo());
    VerifyContext::settle(50);
    check(trigger_count(ctx.song) == before);
    check(ctx.song.redo());
    VerifyContext::settle(50);
    check(trigger_count(ctx.song) == applied);
    ctx.reached("one undo takes the generation back");

    // A second line, sent with the first as history: the model is asked to
    // add, and whatever it chose is written exactly as proposed.
    check(ask_through_bar(ctx, field, QStringLiteral("add offbeat hats")));
    const int second = ctx.llm.proposalCount();
    check(second > 0);
    details << "second answer: " << ctx.llm.proposalSummary().toStdString() << "; ";
    const bool second_adds = ctx.llm.proposalSummary().contains(QStringLiteral("to add to"));
    ctx.click_at(apply, {apply->width() / 2, apply->height() / 2}, Qt::LeftButton);
    VerifyContext::settle(80);
    check(trigger_count(ctx.song) ==
          (second_adds ? applied : 0) + static_cast<std::size_t>(second));
    check(inside_pattern(ctx.song, steps));
    ctx.reached("a follow-up line is answered and applied");

    details << trigger_count(ctx.song) << " triggers on the pattern via "
            << ctx.llm.backendName().toStdString();
    check(ctx.save_screenshot());
    ctx.finish(details.str());
}

[[maybe_unused]] const bool registered = register_scenario("llm_live", run_llm_live);

} // namespace
} // namespace blokkily::verify
