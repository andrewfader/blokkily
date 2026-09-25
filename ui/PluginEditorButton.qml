import QtQuick

// The E on a mixer strip: opens the track's instrument in its own window,
// and is lit while that window is open.
Chip {
    required property int track
    // Whether the strip's track carries an instrument at all.
    property bool available: true
    objectName: "editor" + track
    text: "E"
    accent: Theme.acid
    enabled: available
    on: appController.openEditors.indexOf(track) >= 0
    onClicked: appController.toggleEditor(track)
}
