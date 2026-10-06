import QtQuick
import QtTest
import "../../src/ui/effects"
import "../../src/ui/timeline"

Item {
    width: 960; height: 600
    ListModel { id: chain }
    ListModel {
        id: parameters
        Component.onCompleted: {
            append({id: "gain", name: "Gain", unit: "dB", minimum: -80, maximum: 24, value: 0, defaultValue: 0, skew: 1, normalized: 0.7692307692});
            append({id: "pan", name: "Pan", unit: "", minimum: -1, maximum: 1, value: 0, defaultValue: 0, skew: 1, normalized: 0.5});
            append({id: "invert", name: "Invert polarity", unit: "", minimum: 0, maximum: 1, value: 0, defaultValue: 0, skew: 1, normalized: 0});
        }
    }
    QtObject {
        id: stubEffects
        property int polls: 0
        function trackName(track) { return "Test track"; }
        function trackChainModel(track) { return chain; }
        function parametersModel(track, slot) { return parameters; }
        function availableEffects() { return [{id: "gain_pan", displayName: "Gain / Pan"}]; }
        function addEffect(track, type) { chain.append({typeId: type, name: "Gain / Pan", bypassed: false, slotIndex: chain.count}); }
        function removeEffect(track, slot) { chain.remove(slot); }
        function moveEffect(track, from, to) { chain.move(from, to, 1); }
        function setBypassed(track, slot, value) { chain.setProperty(slot, "bypassed", value); }
        function setParameter(track, slot, id, value) {
            for (let i = 0; i < parameters.count; ++i) {
                let p = parameters.get(i);
                if (p.id === id) {
                    parameters.setProperty(i, "value", value);
                    parameters.setProperty(i, "normalized", (value - p.minimum) / (p.maximum - p.minimum));
                }
            }
        }
        function setNormalized(track, slot, id, value) {
            for (let i = 0; i < parameters.count; ++i) {
                let p = parameters.get(i);
                if (p.id === id) setParameter(track, slot, id, p.minimum + value * (p.maximum - p.minimum));
            }
        }
        function resetParameter(track, slot, id) { setParameter(track, slot, id, 0); }
        function gainReduction(track, slot) { polls++; return 0; }
    }
    TimelineView { width: parent.width; height: parent.height }
    EffectRack { id: rack; anchors.fill: parent; controller: stubEffects; trackId: 1 }
    GainReductionMeter { id: meter; controller: stubEffects; trackId: 1; slotIndex: 0; visible: false }
    TestCase {
        name: "EffectsRack"
        when: windowShown
        function test_meter_stops_when_hidden() {
            meter.visible = true;
            tryVerify(() => stubEffects.polls > 0);
            meter.visible = false;
            let polls = stubEffects.polls;
            wait(100);
            compare(stubEffects.polls, polls);
        }
        function test_add_edit_bypass_remove() {
            let add = findChild(rack, "add-effect");
            verify(add !== null);
            mouseClick(add);
            let picker = findChild(add, "effect-picker");
            tryVerify(() => picker.opened);
            let entry = picker.itemAt(0);
            mouseClick(entry);
            tryCompare(chain, "count", 1);
            let slider = null;
            tryVerify(() => { slider = findChild(rack, "parameter-gain"); return slider !== null; });
            slider.forceActiveFocus();
            let before = parameters.get(0).value;
            keyClick(Qt.Key_Left);
            verify(parameters.get(0).value < before);
            mouseDoubleClickSequence(slider, slider.width / 2, slider.height / 2);
            tryCompare(parameters.get(0), "value", 0);
            mouseClick(findChild(rack, "bypass-effect"));
            compare(chain.get(0).bypassed, true);
            verify(!slider.enabled);
            mouseClick(findChild(rack, "remove-effect"));
            tryCompare(chain, "count", 0);
        }
    }
}
