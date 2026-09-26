#include "verify/harness.hpp"
#include "sampler_processor.hpp"
#include "blokkily/audio/bounce.hpp"
#include "blokkily/instruments/sampler_program.hpp"
#include "blokkily/effects/builtin.hpp"

#include <QCoreApplication>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <numbers>
#include <source_location>

namespace blokkily::verify {
namespace {
// Independent driver of the production engine, including its stopped tail.
// It never calls the export function; the written file is compared to this.
std::vector<float> render_mix(SongEngine& engine, std::uint64_t tail) {
    engine.set_metronome_enabled(false); engine.set_recording(false);
    engine.reset_processing(); engine.rewind(); engine.set_playing(true);
    const auto latency=engine.output_latency();
    const auto length=engine.song_samples();
    const auto total=length+std::max(tail,engine.effect_tail_samples())+latency;
    std::vector<float> mix((total-latency)*2),left(512),right(512);
    for(std::uint64_t at=0;at<total;) {
        if(at==length)engine.set_playing(false);
        const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(512,
            at<length?length-at:total-at));
        engine.process({{left.data(),count},{right.data(),count}});
        for(std::size_t f=0;f<count;++f)if(at+f>=latency) {
            mix[2*(at+f-latency)]=left[f];mix[2*(at+f-latency)+1]=right[f];
        }
        at+=count;
    }
    engine.set_playing(false);return mix;
}
void run_everything(VerifyContext& ctx) {
    auto& c=ctx.controller;auto& model=ctx.song;
    const auto check=[&](bool ok,std::source_location loc=std::source_location::current()) {
        if(!ok)std::cerr<<"everything failed at "<<loc.line()<<'\n';ctx.check(ok);
    };
    const auto folder=std::filesystem::path(ctx.parser.value("project").toStdString()).parent_path();
    std::filesystem::create_directories(folder);
    const auto sample=folder/"tones.wav";
    std::vector<float> tone(48000);
    for(std::size_t i=0;i<tone.size();++i)tone[i]=0.15F*std::sin(2*std::numbers::pi*440*i/48000.0);
    WaveWriter writer;
    check(writer.open(sample,48000,WaveFormat::float32)&&writer.write(tone,tone)&&writer.close());
    // The real browser scan is exercised before the complete canonical song.
    check(c.verifyClap(ctx.parser.value("clap-fixture")));
    c.scanPluginPaths({ctx.parser.value("clap-fixture").toStdString()},
        {ctx.parser.value("vst3-fixture").toStdString()},
        {ctx.parser.value("soundfont-fixture").toStdString()});
    ctx.output->stop();ctx.output->model_input(2,128,false);
    Song song;
    song.tempo.points={{0,120,true},{1920,160,false}};
    song.meter.changes={{0,4,4},{1,7,8}};
    song.tracks.resize(7);song.patterns.clear();song.clips.clear();
    const std::array<std::string,7> names={"KEYED","SLICES","CLAP","VST3","SOUNDFONT","WARPED AUDIO","AUDIO INPUT"};
    for(std::size_t i=0;i<song.tracks.size();++i) {
        song.tracks[i].name=names[i];song.tracks[i].mix.gain_db=-12;
        song.patterns.push_back({names[i],Pattern(3600,480)});
        if(i<5) {
            for(int n=0;n<4;++n) {
                Trigger trigger;trigger.start=n*480+static_cast<Tick>(i)*60;
                trigger.duration=180;trigger.musical_data=Note{static_cast<std::int16_t>(i==1?36+n:60+i),0.65F};
                (void)song.patterns[i].pattern.add(trigger);
            }
            song.clips.push_back({i,i,0,1});
        }
    }
    auto keyed=default_sampler(SamplerProgram::Mode::keyed);keyed.zones.emplace_back();
    keyed.zones[0].sample=sample.string();keyed.zones[0].root_key=60;
    song.tracks[0].instrument={sampler_format,"",sampler_keyed_identifier,serialize_sampler(keyed)};
    auto kit=default_sampler(SamplerProgram::Mode::kit);
    check(slice_evenly(kit,keyed.zones[0],48000,4,36));
    song.tracks[1].instrument={sampler_format,"",sampler_kit_identifier,serialize_sampler(kit)};
    song.tracks[2].instrument={"CLAP",ctx.parser.value("clap-fixture").toStdString(),"dev.blokkily.test",{}};
    song.tracks[3].instrument={"VST3",ctx.parser.value("vst3-fixture").toStdString(),"",{}};
    song.tracks[4].instrument={"SoundFont",ctx.parser.value("soundfont-fixture").toStdString(),"",{}};
    const EffectSlot clap_fx{{"CLAP",ctx.parser.value("clap-effect-fixture").toStdString(),"dev.blokkily.test.effect",{}},false,{}};
    song.tracks[2].inserts={clap_fx};
    song.tracks[3].inserts={{{"VST3",ctx.parser.value("vst3-effect-fixture").toStdString(),"",{}},false,{}}};
    song.tracks[0].inserts={{{std::string(builtin_effect_format),"","eq3",{}},false,{}}};
    song.returns.resize(1);song.returns[0].name="AUX";song.returns[0].inserts={clap_fx};
    song.tracks[0].sends={{0,-12,false}};
    song.master_inserts={{{std::string(builtin_effect_format),"","eq3",{}},false,{}}};
    song.tracks[0].automation={{{AutomationTarget::Kind::gain,0,{},track_instrument(0)},{{0,-18},{1920,-6}}}};
    song.tracks[2].automation={{{AutomationTarget::Kind::parameter,0,{}, {BusKind::track,2,0}},{{0,0.2},{1920,0.8}}}};
    song.tracks[2].input.armed=true;song.tracks[3].input.armed=true;
    song.tracks[6].input={true,TrackInput::Source::audio,-1,0,2,TrackInput::Monitor::off};
    song.audio_files={{sample,48000,48000,2}};
    AudioClip clip;clip.id=1;clip.track=5;clip.length_frames=48000;
    clip.warp.follow_tempo=true;clip.warp.source_bpm=100;
    song.audio_clips={clip};song.metronome.enabled=true;song.metronome.count_in_bars=1;
    model.replace(std::move(song));c.flushRecompile();c.waitForWarpRenders();
    check(c.engine() && c.engine()->processor(track_instrument(4)));
    check(c.warpRendered()>0 && c.warpRendersPending()==0);
    check(c.selectMidiPort(0));
    model.selectTrack(2);model.selectRack("master",0);c.toggleRackOutputRecording();
    c.rewindPlayback();c.toggleRecord();c.togglePlayback();
    std::vector<float> stereo(2048),input(2048);
    for(int block=0;block<160;++block) {
        if(block==105) { const std::array<std::uint8_t,3> note{0x90,72,96};check(c.midiInput().inject(note)); }
        if(block==112) { const std::array<std::uint8_t,3> note{0x80,72,0};check(c.midiInput().inject(note)); }
        for(std::size_t f=0;f<1024;++f) {
            const float value=block>=105 && block<125 ? 0.2F*std::sin(2*std::numbers::pi*330*(block*1024+f)/48000.0):0;
            input[f]=value;input[1024+f]=value;
        }
        check(ctx.output->pump(stereo,input));QCoreApplication::processEvents();
    }
    c.togglePlayback();c.toggleRecord();c.toggleRackOutputRecording();
    check(model.song().patterns[2].pattern.events().size()>4);
    check(model.song().patterns[3].pattern.events().size()>4);
    check(model.song().audio_clips.size()>=3 && model.trackCount()==8);
    const auto recorded_clips=model.song().audio_clips.size();
    check(model.undo());check(model.trackCount()==7 && model.song().audio_clips.size()==1);
    check(model.redo());check(model.song().audio_clips.size()==recorded_clips && model.trackCount()==8);
    ctx.reached("everything: notes on two tracks, input audio and master resample form one take");
    check(c.saveProject(ctx.parser.value("project")));
    c.newProject();check(c.loadProject(ctx.parser.value("project")));c.waitForWarpRenders();c.flushRecompile();
    check(model.trackCount()==8 && model.song().meter.changes[1].numerator==7);
    check(model.song().metronome.enabled && model.song().audio_clips.size()==recorded_clips);
    ctx.output->stop();
    // Save/reopen produces new instances for every format. The bounce must
    // still be exactly this engine's mix, including each recorded clip.
    const auto expected=render_mix(*c.engine(),24000);
    check(c.exportAudioFile(ctx.parser.value("export"),"FLOAT32"));
    const auto wave=read_wave(ctx.parser.value("export").toStdString());check(wave.has_value());
    if(wave) {
        check(wave->interleaved.size()==expected.size());
        std::size_t differences=0;
        for(std::size_t i=0;i<std::min(expected.size(),wave->interleaved.size());++i)
            differences+=expected[i]!=wave->interleaved[i];
        if(differences)std::cerr<<"everything bounce differs in "<<differences<<" samples\n";
        check(differences==0);
    }
    ctx.reached("everything: read-back export equals reopened engine sample for sample");
    // Every track, including the SoundFont and recorded audio, must actually
    // contribute to the rendered bus. Compare its absence, not a UI flag.
    const auto complete=model.song();
    for(std::size_t track=0;track<complete.tracks.size();++track) {
        auto muted=complete;muted.tracks[track].mix.mute=true;
        c.engine()->apply_mix(muted);
        const auto without=render_mix(*c.engine(),24000);
        double delta=0;for(std::size_t i=0;i<expected.size();++i)delta+=std::abs(expected[i]-without[i]);
        if(delta<0.01)std::cerr<<"inaudible track "<<track<<'\n';check(delta>0.01);
        c.engine()->apply_mix(complete);
    }
    ctx.reached("everything: every instrument and recorded clip contributes audio");
    model.selectTrack(0);model.selectRack("track",0);

    QCoreApplication::processEvents();(void)ctx.window->grabWindow();
    for(const auto name:{"effectRack","mixerPanel","arrangementView"}) {
        auto* item=ctx.named(name);if(item)check(VerifyContext::usable(item,180,80));
    }
    check(VerifyContext::usable(ctx.named("automationLaneArea"),300,40));
    check(ctx.save_screenshot());ctx.finish("all formats, tempo/meter, warp, automation, takes and export parity");
}
[[maybe_unused]]const bool registered=register_scenario("everything",run_everything);
}
}
