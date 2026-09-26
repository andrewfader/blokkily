#include "realtime_case.hpp"
#include "../support/audio_probe.hpp"
#include "blokkily/audio/song_engine.hpp"
#include "blokkily/plugins/clap_instance.hpp"

BLOKKILY_REALTIME_CASE(output_recording) {
    using namespace blokkily;
    using namespace blokkily::realtime;
    Song song;
    song.patterns={{"P",Pattern(1920,480)}};
    song.tracks.resize(1); song.clips={{0,0,0,1}};
    song.returns.resize(1); song.tracks[0].sends={{0,0,false}};
    Trigger trigger; trigger.duration=960; trigger.musical_data=Note{60,1.0F};
    (void)song.patterns[0].pattern.add(trigger);
    SongEngine engine;
    engine.set_instrument(0,ClapPluginInstance::create(BLOKKILY_TEST_CLAP_PATH,"dev.blokkily.test"));
    require(engine.prepare(song,48000,256),"prepare output recording");
    SampleRing ring(256*8);
    std::vector<float> left(256),right(256),captured;
    engine.set_playing(true);engine.set_recording(true);
    test::AllocationCount total;
    for(const auto kind:{BusKind::track,BusKind::ret,BusKind::master}) {
        require(engine.configure_output_capture({kind,0},&ring),"select output");
        engine.set_bounce_tap(OutputTap{kind,0});
        for(int i=0;i<100;++i) {
            test::AllocationGuard guard;
            engine.process({left,right}); total+=guard.count();
        }
    }
    engine.disconnect_output_capture();engine.set_bounce_tap({});
    require_no_allocations(total,"track/return/master capture, stem rendering and full ring");
    require(ring.dropped_frames()>0,"overflow is bounded");
    CaptureHeader header; bool heard=false;
    while(ring.pop(header,captured)) for(float value:captured) heard|=value>0.01F;
    require(heard,"ring contains rendered audio");
}
