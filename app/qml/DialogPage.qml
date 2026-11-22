import QtQuick
import Lomiri.Components

// Stage 4.3: Steam waits for the user before starting a game (a notice, a publisher's agreement):
// its dialog, from hidden Steam's screen, shown here. A tap on the picture is a tap on the dialog;
// the user answers it, UTDeck only passes the taps on. The page closes when Steam goes on.
Page {
    id: page

    header: PageHeader {
        id: pageHeader

        title: i18n.tr("Steam is asking")
    }

    Label {
        id: intro

        anchors {
            top: pageHeader.bottom
            left: parent.left
            right: parent.right
            margins: units.gu(2)
        }
        wrapMode: Text.Wrap
        text: i18n.tr("Steam waits for your answer before it starts the game. Tap the dialog below as you would on a computer.")
    }

    Image {
        id: dialog

        anchors {
            top: intro.bottom
            left: parent.left
            right: parent.right
            bottom: parent.bottom
            margins: units.gu(2)
        }
        source: steamLaunch.waitingForUser ? "image://steamdialog/" + steamLaunch.dialogRevision : ""
        sourceSize: Qt.size(width, height)
        fillMode: Image.PreserveAspectFit
        cache: false

        MouseArea {
            anchors.fill: parent
            onClicked: (mouse) => {
                const x0 = (dialog.width - dialog.paintedWidth) / 2
                const y0 = (dialog.height - dialog.paintedHeight) / 2
                steamLaunch.tap((mouse.x - x0) / dialog.paintedWidth, (mouse.y - y0) / dialog.paintedHeight)
            }
        }
    }
}
