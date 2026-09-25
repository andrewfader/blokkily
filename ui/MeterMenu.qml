import QtQuick
import QtQuick.Controls

// The meters a bar can take, offered where the bar is: on the ruler and on the
// transport's meter readout. Choosing one makes it take effect from `bar`;
// clips and tempo points keep their bar numbers (decision 9).
Menu {
    id: menu
    // The 0-based bar the menu was opened for.
    property int bar: 0
    readonly property var choices: [
        [2, 4], [3, 4], [4, 4], [5, 4], [6, 4], [7, 4],
        [3, 8], [5, 8], [6, 8], [7, 8], [9, 8], [12, 8]
    ]
    function openFor(bar) {
        menu.bar = bar
        menu.popup()
    }
    Instantiator {
        model: menu.choices
        delegate: MenuItem {
            required property var modelData
            readonly property string meter: modelData[0] + "/" + modelData[1]
            objectName: "meterChoice" + modelData[0] + "_" + modelData[1]
            // The bar's meter now is marked rather than checked, so choosing
            // an entry never toggles a binding away.
            readonly property bool current: {
                var layout = songModel.barLayout[menu.bar]
                return layout !== undefined && layout.numerator === modelData[0]
                       && layout.denominator === modelData[1]
            }
            text: (current ? "\u25cf " : "    ") + meter
            onTriggered: songModel.setMeter(menu.bar, modelData[0], modelData[1])
        }
        onObjectAdded: (index, object) => menu.insertItem(index, object)
        onObjectRemoved: (index, object) => menu.removeItem(object)
    }
}
