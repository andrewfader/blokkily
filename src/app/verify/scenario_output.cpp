#include "verify/harness.hpp"
#include "blokkily/audio/wave_file.hpp"

#include <QCoreApplication>
#include <cmath>
#include <iostream>
#include <source_location>

namespace blokkily::verify {
namespace {
void run_output(VerifyContext& ctx) {
    auto& c=ctx.controller; auto& s=ctx.song;
    const auto check=[&](bool ok,std::source_location loc=std::source_location::current()) {
        if(!ok)std::cerr<<"output failed at "<<loc.line()<<'\n';ctx.check(ok);
    };
    const auto layout=[&]{QCoreApplication::processEvents();(void)ctx.window->grabWindow();};
    check(c.verifyClap(ctx.parser.value("clap-fixture")));
    auto song=s.song();song.tracks.resize(1);song.clips={{0,0,0,2}};song.tracks[0].mix={};
    s.replace(std::move(song));c.flushRecompile();
    check(c.saveProject(ctx.parser.value("project")));
    check(c.exportAudioFile(ctx.parser.value("export"),"FLOAT32"));
    const auto reference=read_wave(ctx.parser.value("export").toStdString());check(reference.has_value());
    s.selectRack("master",0);layout();
    auto* button=ctx.named("recordRackOutput");
    check(VerifyContext::usable(button,70,18));
    if(button)ctx.click_at(button,{button->width()/2,button->height()/2},Qt::LeftButton);
    check(c.outputRecordingArmed());
    c.rewindPlayback();c.toggleRecord();c.togglePlayback();
    for(int i=0;i<60;++i)check(ctx.pump()>=0);
    c.togglePlayback();c.toggleRecord();layout();
    check(s.trackCount()==2 && s.song().audio_clips.size()==1);
    if(s.song().audio_clips.size()==1) {
        const auto& clip=s.song().audio_clips[0];
        const auto take=read_wave(s.song().audio_files[clip.file].path);check(take.has_value());
        if(take && reference) {
            check(take->frames==60*512);
            for(std::size_t frame=0;frame<take->frames;++frame)
                if(take->interleaved[frame*2]!=reference->interleaved[frame*2]) {
                    check(false);std::cerr<<"resample mismatch at "<<frame<<'\n';break;
                }
        }
    }
    ctx.reached("output: master recording matches bounce");
    check(s.undo());check(s.trackCount()==1 && s.song().audio_clips.empty());
    check(s.redo());check(s.trackCount()==2 && s.song().audio_clips.size()==1);
    check(s.undo());
    ctx.reached("output: one undo removes the complete resample");
    c.toggleRackOutputRecording();
    s.selectRack("master",0);check(c.bounceRackInPlace(true));layout();
    check(s.trackCount()==2 && s.song().tracks[0].mix.mute && s.song().audio_clips.size()==1);
    const QString stem_mix=ctx.parser.value("export")+".stem.wav";
    check(c.exportAudioFile(stem_mix,"FLOAT32"));
    const auto replacement=read_wave(stem_mix.toStdString());check(replacement.has_value());
    if(replacement && reference)
        for(std::size_t frame=0;frame<reference->frames;++frame)
            if(std::abs(replacement->interleaved[frame*2]-reference->interleaved[frame*2])>1e-6F) {check(false);break;}
    check(s.undo());check(s.trackCount()==1 && !s.song().tracks[0].mix.mute);
    check(s.redo());
    ctx.reached("output: bounce in place replaces the mix and is undoable");
    check(c.saveProject(ctx.parser.value("project")));check(c.loadProject(ctx.parser.value("project")));
    layout();check(VerifyContext::usable(ctx.named("effectRack"),180,80));
    check(VerifyContext::usable(ctx.named("bounceRackOutput"),40,18));
    check(ctx.save_screenshot());ctx.finish("output recording, stem placement and undo");
}
[[maybe_unused]]const bool registered=register_scenario("output",run_output);
}
}
