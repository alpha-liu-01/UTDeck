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
        // No back arrow: Cancel leaves the question unanswered (and closes it on hidden Steam's side).
        leadingActionBar.actions: [
            Action {
                iconName: "close"
                text: i18n.tr("Cancel")
                onTriggered: steamLaunch.dismiss()
            }
        ]
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
        text: i18n.tr("Steam waits for your answer before it starts the game. Tap the dialog below as you would on a computer; drag up or down to scroll.")
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

        // A tap is a click on the dialog; a drag up or down scrolls it (one wheel notch per 3 grid units).
        MouseArea {
            property real startY: 0
            property real lastY: 0
            property bool dragged: false

            function fx(mx) { return (mx - (dialog.width - dialog.paintedWidth) / 2) / dialog.paintedWidth }
            function fy(my) { return (my - (dialog.height - dialog.paintedHeight) / 2) / dialog.paintedHeight }

            anchors.fill: parent
            onPressed: (mouse) => { startY = mouse.y; lastY = mouse.y; dragged = false }
            onPositionChanged: (mouse) => {
                if (Math.abs(mouse.y - startY) > units.gu(2))
                    dragged = true
                const steps = Math.trunc((lastY - mouse.y) / units.gu(3))
                if (dragged && steps !== 0) {
                    steamLaunch.scroll(fx(mouse.x), fy(mouse.y), steps)
                    lastY -= steps * units.gu(3)
                }
            }
            onReleased: (mouse) => {
                if (!dragged)
                    steamLaunch.tap(fx(mouse.x), fy(mouse.y))
            }
        }
    }
}
