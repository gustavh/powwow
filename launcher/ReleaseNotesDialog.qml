pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Powwow

// the notes of this release of powwow, from its section of the ChangeLog,
// as the manual is shown
PowwowDialog {
    id: dialog

    // the notes, split before their headings, as a TextEdit draws only
    // part of a long text
    property var blocks: []
    // the version of powwow whose notes they are, or empty
    property string version

    width: Math.min(ApplicationWindow.window.width - 80, 760)
    height: Math.min(ApplicationWindow.window.height - 80, 560)
    title: (version !== "" ? qsTr("What’s new in version %1").arg(version)
                           : qsTr("What’s new"))
    standardButtons: Dialog.Close

    onOpened: {
        const text = Launcher.releaseNotesText()
        if (text === "") {
            blocks = [qsTr("The notes of this release of powwow were not"
                           + " found.")]
            return
        }
        const parts = []
        let lines = []
        for (const line of text.split("\n")) {
            if (line.startsWith("#") && lines.length > 0) {
                parts.push(lines.join("\n"))
                lines = []
            }
            lines.push(line)
        }
        parts.push(lines.join("\n"))
        blocks = parts
    }

    ScrollView {
        id: scroll

        anchors.fill: parent
        contentWidth: availableWidth

        Column {
            width: scroll.availableWidth - 16
            spacing: 8

            Repeater {
                model: dialog.blocks

                SelectableText {
                    id: block

                    required property string modelData

                    width: parent.width
                    text: Launcher.markdownHtml(modelData, dialog.dark)
                    wrapMode: TextEdit.WrapAtWordBoundaryOrAnywhere
                    textFormat: TextEdit.RichText
                    onLinkActivated: function(link) {
                        Qt.openUrlExternally(link)
                    }

                    HoverHandler {
                        cursorShape: (block.hoveredLink !== ""
                                      ? Qt.PointingHandCursor
                                      : Qt.IBeamCursor)
                    }
                }
            }
        }
    }
}
