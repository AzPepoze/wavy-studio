import QtQuick
import QtTest
import "../../src/ui/record"

Item {
    width: 200
    height: 100
    QtObject {
        id: controller
        property bool recording: false
        property bool busy: false
        property int armedTrackId: -1
        function armTrack(id, armed) { armedTrackId = armed ? id : -1; }
        function toggleRecord() { recording = !recording; }
    }
    ArmButton { id: arm; controller: controller; trackId: 7; trackName: "Vocals" }
    RecordButton { id: record; x: 80; controller: controller }
    TestCase {
        name: "Recording"
        when: windowShown
        function init() { controller.recording = false; controller.busy = false; controller.armedTrackId = -1; }
        function test_arm() {
            mouseClick(arm);
            compare(controller.armedTrackId, 7);
            verify(arm.checked);
            mouseClick(arm);
            compare(controller.armedTrackId, -1);
        }
        function test_record() {
            mouseClick(record);
            verify(controller.recording);
            verify(!arm.enabled);
            mouseClick(record);
            verify(!controller.recording);
            controller.busy = true;
            verify(!record.enabled);
        }
    }
}
