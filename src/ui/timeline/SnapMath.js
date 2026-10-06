// Tempo-aware grid and ruler maths shared by the editor, ruler and toolbar.
// One beat is a quarter note (see engine Tempo); the denominator only changes the bar length.
.pragma library

// Auto snap candidates as fractions of a beat, finest first. Bars are appended dynamically so the
// grid can grow coarser than a beat when zoomed out.
function autoStepBeats(beatsPerBar, framesPerBeat, pixelsPerFrame, minimumPixels) {
    const bar = beatsPerBar > 0 ? beatsPerBar : 4;
    let candidates = [1 / 64, 1 / 32, 1 / 16, 1 / 8, 1 / 4, 1 / 2, 1, 2, 4];
    for (let bars = 1; bars <= 64; bars *= 2)
        candidates.push(bar * bars);
    candidates.sort((a, b) => a - b);
    let previous = 0;
    for (const step of candidates) {
        if (step <= previous)
            continue;
        previous = step;
        if (step * framesPerBeat * pixelsPerFrame >= minimumPixels)
            return step;
    }
    return candidates[candidates.length - 1];
}

// Fixed division names used by UserSettings and the settings popup.
function fixedStepBeats(division, beatsPerBar) {
    switch (division) {
    case "bar": return beatsPerBar > 0 ? beatsPerBar : 4;
    case "1/2": return 0.5;
    case "1/4": return 0.25;
    case "1/8": return 0.125;
    case "1/16": return 0.0625;
    case "1/32": return 0.03125;
    case "1/2T": return 0.5 * 2 / 3;
    case "1/4T": return 0.25 * 2 / 3;
    case "1/8T": return 0.125 * 2 / 3;
    case "1/16T": return 0.0625 * 2 / 3;
    }
    return 0;
}

function stepBeats(division, beatsPerBar, framesPerBeat, pixelsPerFrame, minimumPixels) {
    if (division === "auto" || !division)
        return autoStepBeats(beatsPerBar, framesPerBeat, pixelsPerFrame, minimumPixels);
    return fixedStepBeats(division, beatsPerBar);
}

function stepFrames(division, beatsPerBar, framesPerBeat, pixelsPerFrame, minimumPixels) {
    return stepBeats(division, beatsPerBar, framesPerBeat, pixelsPerFrame, minimumPixels) *
           framesPerBeat;
}

// Short label for the toolbar readout, for example "1/16", "2 beats" or "Bar".
function stepLabel(division, stepBeatsValue, beatsPerBar) {
    if (division !== "auto" && division)
        return division === "bar" ? "Bar" : division;
    const bar = beatsPerBar > 0 ? beatsPerBar : 4;
    if (stepBeatsValue >= bar - 1e-9) {
        const bars = Math.round(stepBeatsValue / bar);
        return bars === 1 ? "Bar" : bars + " bars";
    }
    if (stepBeatsValue >= 1 - 1e-9)
        return Math.round(stepBeatsValue) + " beats";
    return "1/" + Math.round(1 / stepBeatsValue);
}

// 1-based bar/beat of a frame, with the whole-beat fraction kept for finer subdivisions.
function barsBeats(frame, framesPerBeat, beatsPerBar) {
    const barBeats = beatsPerBar > 0 ? beatsPerBar : 4;
    const beat = frame / framesPerBeat;
    const bar = Math.floor(beat / barBeats);
    const within = beat - bar * barBeats;
    return { bar: bar + 1, beat: Math.floor(within) + 1, fraction: within - Math.floor(within) };
}

function formatTime(frame, sampleRate) {
    const milliseconds = Math.round(frame / sampleRate * 1000);
    const minutes = Math.floor(milliseconds / 60000).toString().padStart(2, "0");
    const seconds = (Math.floor(milliseconds / 1000) % 60).toString().padStart(2, "0");
    const millis = (milliseconds % 1000).toString().padStart(3, "0");
    return minutes + ":" + seconds + "." + millis;
}

function formatBarsBeats(frame, framesPerBeat, beatsPerBar) {
    const position = barsBeats(frame, framesPerBeat, beatsPerBar);
    return position.bar + ":" + position.beat;
}
