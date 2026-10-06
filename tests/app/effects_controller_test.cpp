#include "EffectsController.hpp"
#include "SnapshotPublisher.hpp"
#include "TimelineModel.hpp"
#include "audio/Mixer.hpp"
#include "effects/Compressor.hpp"
#include "timeline/Commands.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <array>
#include <doctest/doctest.h>
#include <numbers>
#include <thread>

namespace {
void settle(int milliseconds = 100) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < milliseconds) {
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
}
} // namespace

TEST_CASE("effect chains and parameter models follow structural and atomic edits") {
    TimelineModel timeline;
    EffectsController effects(timeline);
    const int track = timeline.trackIdAt(0);
    auto* chain = effects.trackChainModel(track);
    int changes = 0;
    QObject::connect(&effects, &EffectsController::structureChanged, [&] { ++changes; });
    effects.addEffect(track, "gain_pan");
    effects.addEffect(track, "parametric_eq");
    effects.addEffect(track, "compressor");
    CHECK(chain->rowCount() == 3);
    effects.moveEffect(track, 2, 0);
    CHECK(chain->data(chain->index(0, 0), EffectsListModel::TypeId) == "compressor");
    effects.setBypassed(track, 0, true);
    CHECK(chain->data(chain->index(0, 0), EffectsListModel::Bypassed).toBool());
    effects.removeEffect(track, 2);
    CHECK(chain->rowCount() == 2);
    CHECK(changes == 0);
    settle();
    CHECK(changes == 1);
    auto* parameters = effects.parametersModel(track, 1);
    effects.setParameter(track, 1, "gain", -6);
    const auto& chainEntries = effects.chains().tracks.at({static_cast<unsigned>(track)});
    CHECK(chainEntries[1].params->get("gain") == -6);
    CHECK(parameters->data(parameters->index(0, 0), EffectsListModel::Value).toFloat() == -6);
    effects.resetParameter(track, 1, "gain");
    CHECK(chainEntries[1].params->get("gain") == 0);
    effects.setNormalized(track, 1, "pan", .75);
    CHECK(chainEntries[1].params->get("pan") == doctest::Approx(.5));
    settle();
    CHECK(changes == 1);
    effects.removeEffect(track, 1);
    CHECK(parameters->rowCount() == 0);
    effects.addEffect(0, "gain_pan");
    CHECK(effects.chains().master.size() == 1);
    effects.addEffect(-1, "gain_pan");
    effects.addEffect(track, "unknown");
    CHECK(chain->rowCount() == 1);
    effects.moveEffect(track, -1, 0);
    effects.removeEffect(track, 100);
    CHECK(chain->rowCount() == 1);
}

TEST_CASE("effect slider mapping round trips skew and finite gain domain") {
    wavy::effects::EffectFactory factory;
    for (const auto& entry : factory.entries()) {
        auto slot = factory.slot(entry.id);
        for (const auto& p : slot.params->definitions()) {
            for (double normalized : {0., .01, .25, .5, .75, 1.}) {
                const auto value = EffectsController::fromNormalized(p, normalized);
                CHECK(EffectsController::normalized(p, value) ==
                      doctest::Approx(normalized).epsilon(.00001));
            }
            CHECK(EffectsController::fromNormalized(
                      p, EffectsController::normalized(p, p.defaultValue)) ==
                  doctest::Approx(p.defaultValue));
        }
    }
}

TEST_CASE("undoing added tracks drops their chains") {
    TimelineModel timeline;
    EffectsController effects(timeline);
    timeline.addTrack("Effects");
    const auto track = timeline.trackIdAt(timeline.rowCount() - 1);
    auto* model = effects.trackChainModel(track);
    effects.addEffect(track, "compressor");
    REQUIRE(model->rowCount() == 1);
    timeline.undo();
    CHECK(model->rowCount() == 0);
    CHECK_FALSE(effects.chains().tracks.contains({static_cast<unsigned>(track)}));
    timeline.redo();
    CHECK(model->rowCount() == 0);
}

TEST_CASE("structural effect edits publish once while parameter edits do not publish") {
    TimelineModel timeline;
    for (int row = 0; row < timeline.rowCount(); ++row) {
        const auto track = timeline.trackIdAt(row);
        for (const auto& clip : timeline.visibleClips(track, 0, timeline.durationFrames()))
            timeline.action(clip.toMap().value("clipId").toInt(), track, "delete");
    }
    wavy::Mixer mixer;
    EffectsController effects(timeline);
    SnapshotPublisher publisher(timeline, mixer);
    publisher.setEffectsController(effects);
    int publications = 0;
    QObject::connect(&publisher, &SnapshotPublisher::published, [&] { ++publications; });
    settle(250);
    const auto before = publications;
    effects.addEffect(1, "gain_pan");
    effects.addEffect(1, "compressor");
    effects.moveEffect(1, 0, 1);
    effects.setBypassed(1, 0, true);
    effects.removeEffect(1, 1);
    settle();
    CHECK(publications == before + 1);
    effects.setParameter(1, 0, "threshold", -40);
    settle();
    CHECK(publications == before + 1);
}

TEST_CASE("controller chains change offline gain and EQ and bypass restores dry audio") {
    TimelineModel timeline;
    timeline.addTrack("Tone");
    const auto track = timeline.trackIdAt(timeline.rowCount() - 1);
    timeline.setTrackState(timeline.rowCount() - 1, "solo", true);
    timeline.addClip(track, "tone", 0, 4800);
    auto tone = std::make_shared<wavy::AudioBuffer>();
    tone->sampleRate = 48000;
    tone->channels = 2;
    for (int i = 0; i < 4800; ++i) {
        const float sample = .1 * std::sin(2 * std::numbers::pi * 1000 * i / 48000.);
        tone->samples.insert(tone->samples.end(), {sample, sample});
    }
    wavy::SourceCache cache{{"tone", tone}};
    for (const auto& t : timeline.timeline().tracks())
        for (const auto& clip : t.clips)
            if (!cache.contains(clip.source))
                cache.emplace(clip.source, nullptr);
    EffectsController effects(timeline);
    auto level = [&] {
        wavy::Mixer mixer;
        auto snapshot = wavy::buildSnapshot(timeline.timeline(), cache, effects.chains());
        effects.observeSnapshot(*snapshot);
        mixer.publish(std::move(snapshot));
        mixer.transport().play();
        std::vector<float> output(9600);
        mixer.render(output.data(), 4800);
        double sum = 0;
        for (int i = 2400; i < 4800; ++i)
            sum += output[i * 2] * output[i * 2];
        return std::sqrt(sum / 2400);
    };
    const auto dry = level();
    effects.addEffect(track, "gain_pan");
    // Hard left avoids the engine's constant-power center attenuation in this gain proof.
    effects.setParameter(track, 0, "pan", -1);
    effects.setParameter(track, 0, "gain", -6);
    CHECK(level() / dry == doctest::Approx(std::pow(10., -6. / 20)).epsilon(.001));
    effects.setBypassed(track, 0, true);
    CHECK(level() == doctest::Approx(dry));
    effects.removeEffect(track, 0);
    effects.addEffect(track, "parametric_eq");
    effects.setParameter(track, 0, "band2.frequency", 1000);
    effects.setParameter(track, 0, "band2.gain", 6);
    CHECK(level() / dry == doctest::Approx(std::pow(10., 6. / 20)).epsilon(.01));
    effects.setBypassed(track, 0, true);
    CHECK(level() == doctest::Approx(dry));
    effects.removeEffect(track, 0);
    effects.addEffect(track, "compressor");
    effects.setParameter(track, 0, "threshold", -40);
    level();
    CHECK(effects.gainReduction(track, 0) > 0);
    effects.removeEffect(track, 0);
    CHECK(effects.gainReduction(track, 0) == 0);
}

TEST_CASE("atomic effect parameters can be edited during offline rendering") {
    TimelineModel timeline;
    EffectsController effects(timeline);
    effects.addEffect(1, "gain_pan");
    auto prepared = wavy::effects::EffectFactory{}.create(
        "gain_pan", effects.chains().tracks.at({1})[0].params);
    prepared->prepare(48000, 64);
    std::thread render([&] {
        std::array<float, 128> samples{};
        for (int i = 0; i < 10000; ++i)
            prepared->process(samples.data(), 64);
    });
    for (int i = 0; i < 10000; ++i)
        effects.setParameter(1, 0, "gain", i % 24 - 12);
    render.join();
    CHECK(prepared->parameters().get("gain") == 3);
}

TEST_CASE("Effects controller retains prepared slots and meter ownership across republishes") {
    TimelineModel timeline;
    EffectsController effects(timeline);
    const int track = timeline.trackIdAt(0);
    effects.addEffect(track, "compressor");
    wavy::SourceCache cache;
    for (const auto& t : timeline.timeline().tracks())
        for (const auto& clip : t.clips)
            cache.emplace(clip.source, nullptr);
    auto first = wavy::buildSnapshot(timeline.timeline(), cache, effects.chains());
    effects.observeSnapshot(*first);
    auto second = wavy::buildSnapshot(timeline.timeline(), cache, effects.chains());
    effects.observeSnapshot(*second);
    REQUIRE(first->tracks[0].effects.size() == 1);
    CHECK(first->tracks[0].effects[0].effect == second->tracks[0].effects[0].effect);
    effects.setParameter(track, 0, "threshold", -30);
    effects.setBypassed(track, 0, true);
    auto bypassed = wavy::buildSnapshot(timeline.timeline(), cache, effects.chains());
    CHECK(bypassed->tracks[0].effects[0].effect == first->tracks[0].effects[0].effect);
    CHECK(bypassed->tracks[0].effects[0].bypassed);
    CHECK(bypassed->tracks[0].effects[0].effect->parameters().get("threshold") == -30);
    effects.removeEffect(track, 0);
    CHECK(effects.gainReduction(track, 0) == 0);
    CHECK(first->tracks[0].effects[0].effect.use_count() >= 3);
}

TEST_CASE("Snapshot publisher consumes the controller chain without copying its slots") {
    TimelineModel timeline;
    EffectsController effects(timeline);
    const int track = timeline.trackIdAt(0);
    effects.addEffect(track, "compressor");
    effects.setParameter(track, 0, "threshold", -40);
    const auto& slot = effects.chains().tracks.at({static_cast<unsigned>(track)})[0];
    auto instance = slot.prepared(timeline.timeline().sampleRate, 512);
    wavy::Mixer mixer;
    SnapshotPublisher publisher(timeline, mixer);
    publisher.setEffectsController(effects);
    int publications = 0;
    QObject::connect(&publisher, &SnapshotPublisher::published, [&] { ++publications; });
    settle();
    REQUIRE(publications > 0);
    // A slow machine may still be decoding the clip; wait so the compressor is guaranteed input.
    publisher.sourceLibrary().waitIdle();
    settle();
    CHECK(slot.prepared(timeline.timeline().sampleRate, 512) == instance);
    mixer.transport().play();
    std::array<float, 1024> block;
    for (int i = 0; i < 20; ++i)
        mixer.render(block.data(), 512);
    auto* compressor = dynamic_cast<wavy::effects::Compressor*>(instance.get());
    REQUIRE(compressor);
    REQUIRE(effects.gainReduction(track, 0) > 0);
    CHECK(effects.gainReduction(track, 0) == compressor->gainReductionDb().load());
    timeline.addTrack("republish");
    settle();
    CHECK(publications >= 2);
    for (int i = 0; i < 20; ++i)
        mixer.render(block.data(), 512);
    CHECK(effects.gainReduction(track, 0) == compressor->gainReductionDb().load());
}
