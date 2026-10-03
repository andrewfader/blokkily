#include "verify/harness.hpp"
#include "sampler_processor.hpp"
#include "blokkily/audio/wave_file.hpp"
#include "blokkily/instruments/sampler_program.hpp"
#include "blokkily/plugins/dynamic_library.hpp"
#include "blokkily/project/paths.hpp"

#include <QCoreApplication>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <source_location>

namespace blokkily::verify {
namespace {
void run_cross(VerifyContext& ctx) {
    auto& controller=ctx.controller;
    auto& model=ctx.song;
    const auto check=[&](bool ok,std::source_location loc=std::source_location::current()) {
        if(!ok)std::cerr<<"cross failed at "<<loc.line()<<'\n';
        ctx.check(ok);
    };
    const auto layout=[&] { QCoreApplication::processEvents(); (void)ctx.window->grabWindow(); };
    const auto click=[&](const QString& name) {
        auto* item=ctx.named(name); check(VerifyContext::usable(item,18,18));
        if(item)ctx.click_at(item,{item->width()/2,item->height()/2},Qt::LeftButton);
        layout();
    };
    check(controller.verifyClap(ctx.parser.value("clap-fixture")));
    auto configured=model.song();
    configured.tracks.resize(1); configured.clips={{0,0,0,1}};
    configured.tracks[0].mix={};
    const EffectSlot effect{{"CLAP",ctx.parser.value("clap-effect-fixture").toStdString(),
                             "dev.blokkily.test.effect",{}},false,{}};
    configured.tracks[0].inserts={effect};
    configured.returns.resize(1); configured.returns[0].inserts={effect};
    configured.master_inserts={effect};
    model.replace(std::move(configured)); controller.flushRecompile(); layout();
    check(controller.selectMidiPort(0));
    ctx.reached("cross: real effects on each bus");
    void* library=blokkily::dynamic_library::open_loaded(ctx.parser.value("clap-effect-fixture").toStdString());
    auto turn=library?reinterpret_cast<void(*)(double)>(blokkily::dynamic_library::symbol(library,"blokkily_test_effect_turn")):nullptr;
    check(turn!=nullptr);
    for(const auto kind:{QString("track"),QString("return"),QString("master")}) {
        model.selectRack(kind,0); layout();
        check(VerifyContext::usable(ctx.named("effectRack"),180,100));
        click("insertEditor0");
        check(controller.insertEditorOpen(kind,0,0));
        const auto address=ProcessorAddress{kind=="track"?BusKind::track:kind=="return"?BusKind::ret:BusKind::master,0,0};
        auto* plugin=controller.engine()->processor(address);
        check(plugin!=nullptr);
        auto before=plugin->save_state();
        if(turn)turn(0.6);
        (void)ctx.pump(); controller.serviceEditors();
        const auto changed=plugin->save_state();
        check(before!=changed && song_slot(model.song(),address)->state==changed);
        check(model.undo()); layout();
        check(plugin==controller.engine()->processor(address) && plugin->save_state()==before);
        check(model.redo()); layout(); check(plugin->save_state()==changed);
        click("insertAutomation0"); VerifyContext::settle(50);
        click("insertParameter0_0");
        check(!model.automationLanes().empty());
        check(controller.toggleInsertEditor(kind,0,0));
    }
    if(library)blokkily::dynamic_library::close(library);
    ctx.reached("cross: track return and master gestures undo and redo");
    // Hear the lane on the track insert: a zero gain mutes the synth through
    // the complete engine. Removing it brings the sound back.
    model.selectTrack(0); model.selectRack("track",0);
    check(controller.automateInsert("track",0,0,0));
    const int lane=model.selectedLane();
    check(model.moveAutomationPoint(0,lane,0,0,0)>=0);
    controller.flushRecompile(); controller.engine()->reset_processing();
    controller.rewindPlayback(); controller.togglePlayback();
    const float quiet=ctx.pump(); check(quiet<1e-6F);
    check(model.moveAutomationPoint(0,lane,0,0,1)>=0); controller.flushRecompile();
    const float loud=ctx.pump(); check(loud>0.01F);
    controller.togglePlayback();
    ctx.reached("cross: an effect lane changes rendered audio");
    // Removing the insert before an open editor moves its address, but the
    // adopted plugin and its window are still the same ones.
    auto shifted=model.song();
    shifted.tracks[0].inserts.insert(shifted.tracks[0].inserts.begin(),
        {{"Built-in", "", "eq3", {}},false,{}});
    shifted.tracks[0].automation.clear();
    model.replace(std::move(shifted));
    check(controller.toggleInsertEditor("track",0,1));
    auto* kept_window=controller.pluginWindows().window({BusKind::track,0,1});
    auto* kept_plugin=controller.engine()->processor({BusKind::track,0,1});
    check(model.removeInsert("track",0,0));layout();
    check(controller.engine()->processor({BusKind::track,0,0})==kept_plugin);
    check(controller.insertEditorOpen("track",0,0));
    check(controller.pluginWindows().window({BusKind::track,0,0})==kept_window);
    controller.pluginWindows().closeAll();
    ctx.reached("cross: editor follows an adopted insert to its new slot");
    // A clip and a sampler share one external file. Collection deduplicates
    // it, preserves originals, survives undo, and saves portable references.
    const auto project=std::filesystem::path(ctx.parser.value("project").toStdString());
    std::filesystem::create_directories(project.parent_path()/"external");
    const auto file=project.parent_path()/"external"/"collect.wav";
    WaveWriter writer; std::vector<float> tone(4800,0.1F);
    check(writer.open(file,48000,WaveFormat::float32)&&writer.write(tone,tone)&&writer.close());
    auto with_audio=model.song();
    Track sampler; sampler.name="COLLECT SAMPLER";
    sampler.instrument={std::string(sampler_format),"",std::string(sampler_keyed_identifier),{}};
    auto program=default_sampler(SamplerProgram::Mode::keyed); program.zones.emplace_back();
    program.zones[0].sample=file.string(); sampler.instrument.state=serialize_sampler(program);
    with_audio.tracks.push_back(sampler);
    with_audio.audio_files={{file,4800,48000,2}};
    AudioClip clip;clip.id=1;clip.track=1;clip.file=0;clip.length_frames=4800;
    with_audio.audio_clips={clip}; model.replace(std::move(with_audio));
    check(controller.saveProject(QString::fromStdString(project.string())));
    const auto original=model.song().audio_files[0].path;
    layout();click("collectAudioButton");
    const auto collected=model.song().audio_files[0].path;
    check(collected!=original && std::filesystem::exists(original) && std::filesystem::exists(collected));
    auto saved_program=parse_sampler(model.song().tracks[1].instrument.state);
    check(saved_program && saved_program->zones[0].sample==collected.string());
    check(model.undo() && model.song().audio_files[0].path==original);
    check(model.redo() && model.song().audio_files[0].path==collected);
    check(controller.saveProject(QString::fromStdString(project.string())));
    check(controller.loadProject(QString::fromStdString(project.string())));
    check(std::filesystem::exists(resolve_project_path(model.song().audio_files[0].path,project.parent_path())));
    ctx.reached("cross: collect audio deduplicates and is undoable");
    model.selectTrack(0);model.selectRack("track",0);layout();
    check(VerifyContext::usable(ctx.named("insertAutomation0"),18,18));
    check(ctx.save_screenshot());ctx.finish("effect editors, automation and collected audio");
}
[[maybe_unused]]const bool registered=register_scenario("cross",run_cross);
}
}
