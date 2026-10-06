// In-memory demo/stress implementation of the documented timeline contract. Set stress for 100 × 200 clips.
import QtQuick

QtObject {
    id: root

    property var clips: ({})
    property int durationFrames: sampleRate * (stress ? 660 : 600)
    property int nextId: 0
    property int playheadFrame: 144000
    property bool ready: false
    property int sampleRate: 48000
    property bool stress: false
    property real tempoBpm: 120
    property int timeSignatureNumerator: 4
    property int timeSignatureDenominator: 4
    readonly property real framesPerBeat: sampleRate * 60 / tempoBpm
    property ListModel tracks: ListModel {
    }

    signal clipsChangedForTrack(int trackId)

    function setTempo(bpm: real): void { tempoBpm = bpm; }
    function setTimeSignature(numerator: int, denominator: int): void {
        timeSignatureNumerator = numerator;
        timeSignatureDenominator = denominator;
    }

    function action(id: int, trackId: int, operation: string): void {
        let list = clips[trackId];
        let index = list.findIndex(c => c.clipId === id);
        if (index < 0)
            return;
        let clip = list[index];
        if (operation === "delete")
            list.splice(index, 1);
        if (operation === "duplicate")
            list.push({
                clipId: nextId++,
                name: clip.name,
                source: clip.source,
                sourceOffset: clip.sourceOffset,
                startFrame: clip.startFrame + clip.durationFrames,
                durationFrames: clip.durationFrames,
                gain: clip.gain,
                trackIndex: clip.trackIndex
            });
        if (operation === "split" && playheadFrame > clip.startFrame && playheadFrame < clip.startFrame + clip.durationFrames) {
            let end = clip.startFrame + clip.durationFrames;
            clip.durationFrames = playheadFrame - clip.startFrame;
            list.push({
                clipId: nextId++,
                name: clip.name,
                source: clip.source,
                sourceOffset: clip.sourceOffset + (playheadFrame - clip.startFrame),
                startFrame: playheadFrame,
                durationFrames: end - playheadFrame,
                gain: clip.gain,
                trackIndex: clip.trackIndex
            });
        }
        clipsChangedForTrack(trackId);
    }
    function editClip(id: int, trackId: int, start: real, duration: real): void {
        for (let key in clips) {
            let index = clips[key].findIndex(c => c.clipId === id);
            if (index < 0)
                continue;
            let clip = clips[key].splice(index, 1)[0];
            clip.startFrame = Math.round(Math.max(0, start));
            clip.durationFrames = Math.round(Math.max(1, duration));
            clip.trackIndex = trackId;
            clips[trackId].push(clip);
            clipsChangedForTrack(Number(key));
            if (Number(key) !== trackId)
                clipsChangedForTrack(trackId);
            return;
        }
    }
    function sourceFor(track: int): string {
        const kind = ["sine", "saw", "square", "noise"][track % 4];
        const hz = kind === "noise" ? 440 : 110 + (track % 8) * 55;
        return "generated:" + kind + ":" + hz + ":0.2:2.4";
    }
    function generate(): void {
        tracks.clear();
        clips = ({});
        nextId = 0;
        let count = stress ? 100 : 4;
        for (let t = 0; t < count; ++t) {
            tracks.append({
                trackId: t,
                name: stress ? "Track " + (t + 1) : ["Drums", "Bass", "Keys", "Vocals"][t],
                muted: false,
                solo: false,
                gain: 0
            });
            clips[t] = [];
            let n = stress ? 200 : (t < 2 ? 3 : 2);
            for (let c = 0; c < n; ++c)
                clips[t].push({
                    clipId: nextId++,
                    name: "Take " + (c + 1),
                    source: sourceFor(t),
                    sourceOffset: 0,
                    startFrame: (c * 3 + t * 0.4) * sampleRate,
                    durationFrames: sampleRate * 2.4,
                    gain: 1,
                    trackIndex: t
                });
            clipsChangedForTrack(t);
        }
    }
    function setTrackGain(trackId: int, db: real): void {
        for (let i = 0; i < tracks.count; ++i)
            if (tracks.get(i).trackId === trackId) {
                tracks.setProperty(i, "gain", db);
                return;
            }
    }
    function setTrackState(index: int, role: string, value: bool): void {
        tracks.setProperty(index, role, value);
    }
    function trackIdAt(index: int): int {
        return tracks.get(index).trackId;
    }
    function visibleClips(trackId: int, first: real, last: real): var {
        return (clips[trackId] || []).filter(c => c.startFrame < last && c.startFrame + c.durationFrames > first);
    }

    Component.onCompleted: {
        generate();
        ready = true;
    }
    onStressChanged: if (ready)
        generate()
}
