import QtQuick
import Lomiri.Components

// Stage 4.3: Steam waits for the user before starting a game (a notice, a publisher's agreement):
// its dialog, from hidden Steam's screen, shown here. A tap on the picture is a tap on the dialog;
// the user answers it, UTDeck only passes the taps on. The page closes when Steam goes on.
//
// Stage 4.8, with a controller: a pointer on the picture (Steam's own focus does not show on it, so
// Tab and Enter cannot be followed: step 1). The left stick moves it smoothly (Controllers' cursorMode),
// the D-pad or a keyboard's arrows in steps; A (Return) clicks where it points; the right stick
// scrolls the dialog there; B (Escape) cancels Steam's dialog, as its Cancel does.
Page {
    id: page

    // Where the pointer is, as a fraction of the picture (the taps' coordinates too).
    property real px: 0.5
    property real py: 0.5
    property bool keyUsed: false
    readonly property bool pointerShown: controllers.controllerUsed || page.keyUsed
    readonly property bool onTop: page.pageStack && page.pageStack.currentPage === page
    property real wheelRest: 0

    function movePointer(dx, dy) {
        if (dialog.paintedWidth <= 0 || dialog.paintedHeight <= 0)
            return
        page.px = Math.max(0, Math.min(1, page.px + dx / dialog.paintedWidth))
        page.py = Math.max(0, Math.min(1, page.py + dy / dialog.paintedHeight))
    }

    function click() {
        steamLaunch.tap(page.px, page.py)
        clickPulse.restart()
    }

    // Where the focus goes when this page comes to the front (Main.qml's focusPage).
    function focusFirst() {
        pointerKeys.forceActiveFocus()
    }

    onOnTopChanged: controllers.cursorMode = page.onTop
    Component.onCompleted: controllers.cursorMode = page.onTop
    Component.onDestruction: controllers.cursorMode = false

    Connections {
        target: controllers
        function onCursorMoved(x, y) {
            if (!page.onTop)
                return
            // Faster the further the stick is pushed (squared): fine moves near the middle.
            const speed = units.gu(1.6)
            page.movePointer(x * Math.abs(x) * speed, y * Math.abs(y) * speed)
        }
    }

    // B on a controller (Escape) cancels Steam's dialog (Escape in its window, as its Cancel),
    // instead of only leaving the page.
    Keys.onEscapePressed: (event) => {
        steamLaunch.cancel()
        event.accepted = true
    }

    // The right stick (wheel events in the middle of the window) or a mouse wheel: the dialog scrolls
    // where the pointer is, one notch per 120 (a wheel's notch).
    WheelHandler {
        target: null
        onWheel: (event) => {
            page.wheelRest += event.angleDelta.y
            const steps = Math.trunc(page.wheelRest / 120)
            if (steps !== 0) {
                page.wheelRest -= steps * 120
                steamLaunch.scroll(page.px, page.py, -steps)
            }
        }
    }

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
        text: page.pointerShown
              ? i18n.tr("Steam waits for your answer (before starting a game, or to install or uninstall one). Move the pointer with the left stick or the D-pad, click with A, scroll with the right stick; B cancels. Tapping works too.")
              : i18n.tr("Steam waits for your answer (before starting a game, or to install or uninstall one). Tap the dialog below as you would on a computer; drag up or down to scroll.")
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
                if (!dragged) {
                    // The pointer follows a tap, so a controller goes on from there.
                    page.px = Math.max(0, Math.min(1, fx(mouse.x)))
                    page.py = Math.max(0, Math.min(1, fy(mouse.y)))
                    steamLaunch.tap(fx(mouse.x), fy(mouse.y))
                }
            }
        }

        // The pointer: a ring with a dot, its centre where A clicks.
        Item {
            id: pointer

            visible: page.pointerShown && dialog.paintedWidth > 0
            x: (dialog.width - dialog.paintedWidth) / 2 + page.px * dialog.paintedWidth - width / 2
            y: (dialog.height - dialog.paintedHeight) / 2 + page.py * dialog.paintedHeight - height / 2
            width: units.gu(3.5)
            height: width

            Rectangle {
                id: ring

                anchors.centerIn: parent
                width: parent.width
                height: width
                radius: width / 2
                color: "transparent"
                border.width: units.dp(3)
                border.color: "white"

                Rectangle {
                    anchors {
                        fill: parent
                        margins: -units.dp(1.5)
                    }
                    radius: width / 2
                    color: "transparent"
                    border.width: units.dp(1.5)
                    border.color: "black"
                }
            }

            Rectangle {
                anchors.centerIn: parent
                width: units.dp(6)
                height: width
                radius: width / 2
                color: theme.palette.normal.focus
                border.width: units.dp(1)
                border.color: "white"
            }

            SequentialAnimation {
                id: clickPulse

                NumberAnimation { target: ring; property: "scale"; to: 0.6; duration: 80 }
                NumberAnimation { target: ring; property: "scale"; to: 1; duration: 120 }
            }
        }
    }

    // The keys: the arrows move the pointer a step (held: repeated by the controller's bridge), Return
    // clicks.
    Item {
        id: pointerKeys

        focus: true
        Keys.onPressed: (event) => {
            const step = units.gu(1.5)
            switch (event.key) {
            case Qt.Key_Left: page.movePointer(-step, 0); break
            case Qt.Key_Right: page.movePointer(step, 0); break
            case Qt.Key_Up: page.movePointer(0, -step); break
            case Qt.Key_Down: page.movePointer(0, step); break
            case Qt.Key_Return:
            case Qt.Key_Enter:
            case Qt.Key_Space:
                page.click()
                break
            default:
                return
            }
            page.keyUsed = true
            event.accepted = true
        }
    }
}
