#include "engine_graph.hpp"
#include "blokkily/audio/bounce.hpp"
#include "blokkily/audio/take_writer.hpp"
#include "blokkily/audio/playback.hpp"
#include "blokkily/audio/audio_clips.hpp"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace blokkily;
namespace {
void require(bool ok, const std::string& message) { if (!ok) throw std::runtime_error(message); }
Song make_song() {
    Song song;
    song.patterns = {{"Output", Pattern(1920, 480)}};
    Trigger trigger;
    trigger.duration = 960;
    trigger.musical_data = Note{60, 1.0F};
    (void)song.patterns[0].pattern.add(trigger);
    song.tracks.assign(2, Track{});
    song.tracks[0].instrument = {"CLAP", BLOKKILY_TEST_CLAP_PATH, "dev.blokkily.test", {}};
    song.tracks[1].instrument = song.tracks[0].instrument;
    song.tracks[0].mix.pan = -0.4;
    song.tracks[1].mix.pan = 0.7;
    song.tracks[0].mix.gain_db = -3;
    song.tracks[0].inserts.push_back({{"CLAP", BLOKKILY_TEST_CLAP_EFFECT_PATH, "dev.blokkily.test.effect", {}}, false, {}});
    song.clips = {{0,0,0,1}, {1,0,0,1}};
    return song;
}
void prepare(SongEngine& engine, const Song& song, const AudioAssets& assets = {}) {
    auto graph = populate_graph(engine, song, {}, {});
    std::string error;
    require(graph.error.empty(), graph.error);
    require(engine.prepare(song,48000,512,0,&error,assets), error);
    load_fresh_state(engine,song,graph);
}
WaveData bounce(SongEngine& engine, const std::filesystem::path& path, std::optional<OutputTap> source={}) {
    std::string error;
    BounceOptions opts; opts.source=source;
    require(bounce_song(engine,path,WaveFormat::float32,0,opts,&error).has_value(),error);
    const auto wave=read_wave(path,&error);
    require(wave.has_value(),error);
    return *wave;
}
void run(const std::string& test) {
    const auto directory=std::filesystem::path(BLOKKILY_OUTPUT_WORK)/test;
    std::filesystem::create_directories(directory);
    auto song=make_song();
    if(test=="returns") {
        song.returns.resize(1);
        song.tracks[0].sends.push_back({0,-6,false});
        song.returns[0].inserts= song.tracks[0].inserts;
    }
    if(test=="soundfont_reset") {
        song.tracks[0].instrument={"SoundFont", BLOKKILY_TEST_SF2, "", {}};
        song.tracks[0].inserts.clear();song.tracks[1].mix.mute=true;
    }
    if(test=="state") {
        Trigger lock;lock.start=480;lock.duration=120;lock.musical_data=Note{62,1.0F};
        lock.locks={{"level",0,0.8,ParameterLock::Kind::automation}};
        (void)song.patterns[0].pattern.add(lock);
    }
    SongEngine engine; prepare(engine,song);
    auto master=bounce(engine,directory/"master.wav");
    if(test=="state" || test=="soundfont_reset") {
        const auto again=bounce(engine,directory/"again.wav");
        require(again.interleaved==master.interleaved,"REGRESSION: repeated export must start in the same processor state (" + test + ")");
    } else if(test=="stems" || test=="returns") {
        auto first=bounce(engine,directory/"first.wav",OutputTap{BusKind::track,0});
        auto second=bounce(engine,directory/"second.wav",OutputTap{BusKind::track,1});
        std::vector<float> ret(master.interleaved.size());
        if(test=="returns") ret=bounce(engine,directory/"return.wav",OutputTap{BusKind::ret,0}).interleaved;
        float peak=0;
        for(std::size_t i=0;i<master.interleaved.size();++i) {
            require(std::abs(master.interleaved[i]-first.interleaved[i]-second.interleaved[i]-ret[i])<1e-6F,"aligned stems sum to master");
            peak=std::max(peak,std::abs(first.interleaved[i]));
        }
        require(peak>0.02F,"inserted, panned, post-fader stem is audible");
        song.tracks[1].mix.mute=true;
        SongEngine isolated; prepare(isolated,song);
        auto only=bounce(isolated,directory/"isolated.wav");
        if(test=="stems") require(only.interleaved==first.interleaved,"track tap equals its isolated bus contribution");
    } else if(test=="live") {
        TakeWriter writer;
        writer.begin(directory/"takes",48000,false);
        require(engine.configure_output_capture({BusKind::master,0},&writer.ring()),"select master");
        engine.reset_processing(); engine.rewind(); engine.set_recording(true); engine.set_playing(true);
        // The guide click is audible live but excluded from the mix tap.
        engine.set_metronome_enabled(true);
        RtAudioOutput pump(RtAudioOutput::Mode::deterministic);
        require(pump.open(engine,48000,512),"open pump"); require(pump.start(),"start pump");
        std::vector<float> stereo(2*512);
        for(int n=0;n<100;++n) { require(pump.pump(stereo),"pump"); writer.drain(); }
        pump.stop(); engine.disconnect_output_capture(); engine.set_recording(false);
        auto takes=writer.finish(); require(takes.size()==1,"one continuous master take");
        const auto take=read_wave(takes[0].file);
        require(take.has_value(),"read recorded WAV");
        const auto latency=engine.output_latency();
        float peak=0;
        for(std::size_t frame=latency;frame<take->frames;++frame)
            for(std::size_t ch=0;ch<2;++ch) {
                const float actual=take->interleaved[2*frame+ch];
                require(actual==master.interleaved[2*(frame-latency)+ch],"live master equals bounce sample for sample");
                peak=std::max(peak,std::abs(actual));
            }
        require(peak>0.05F,"resample sounds");
        // The actual take becomes a clip and replaces the muted sources.
        song.tracks[0].mix.mute=true; song.tracks[1].mix.mute=true;
        song.clips.clear(); song.tracks.emplace_back();
        song.tracks.back().mix.gain_db=20*std::log10(std::sqrt(2.0));
        takes[0].track=2;
        require(commit_take(song,takes[0],engine.published_clock(),latency).has_value(),"place take");
        AudioAssetCache cache;
        auto asset=cache.load(takes[0].file,48000,{});
        SongEngine replay; prepare(replay,song,{asset});
        auto bounced=bounce(replay,directory/"replay.wav");
        for(std::size_t i=0;i<2*(take->frames-latency);++i)
            require(std::abs(bounced.interleaved[i]-master.interleaved[i])<1e-6F,"new clip replaces source mix");
    } else if(test=="invalid") {
        require(!engine.configure_output_capture({BusKind::track,999},nullptr),"invalid tap rejected");
        require(!bounce_song(engine,directory/"invalid.wav",WaveFormat::float32,0,
            BounceOptions{false,OutputTap{BusKind::ret,9}}),"invalid bounce source rejected");
    } else throw std::runtime_error("unknown case");
}
}
int main(int argc,char** argv) {
    try { require(argc==2,"case required"); run(argv[1]); std::cout<<"PASS "<<argv[1]<<'\n'; }
    catch(const std::exception& e) { std::cerr<<"FAIL "<<e.what()<<'\n'; return 1; }
}
