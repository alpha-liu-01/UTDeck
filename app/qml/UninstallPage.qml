import QtQuick
import Lomiri.Components

// Stage 4.9: uninstalling UTDeck (Uninstall, setup/utdeck-uninstall.sh). Before: what is removed and
// what is kept, with sizes (the script's plan, read again when a tick changes); the Steam games and the
// saves are kept unless ticked (the maintainer, 2026-11-23); a confirmation. Then the steps as they
// go. At the end the app is closed by the user: its own settings, and the app itself if chosen, go
// once it has closed.
Page {
    id: page

    property bool confirming: false
    readonly property bool locked: uninstall.started && !uninstall.failed

    function size(kb) {
        if (kb >= 1024 * 1024)
            return i18n.tr("%1 GB").arg((kb / 1024 / 1024).toFixed(1))
        if (kb >= 1024)
            return i18n.tr("%1 MB").arg(Math.round(kb / 1024))
        return i18n.tr("%1 KB").arg(kb)
    }

    function what(key) {
        switch (key) {
        case "utdeck": return i18n.tr("UTDeck's programs and Wine runtimes")
        case "own": return i18n.tr("Your own games: their Windows folders, with their saves")
        case "steamclient": return i18n.tr("Steam's program and its runtimes")
        case "signin": return i18n.tr("Your Steam sign-in on this phone")
        case "games": return i18n.tr("The Steam games")
        case "saves": return i18n.tr("Saved games and game settings")
        case "copied": return i18n.tr("The game folders you copied (Documents/UTDeck/Games)")
        case "entries": return i18n.tr("The games' entries in the app drawer")
        case "appdata": return i18n.tr("UTDeck's own settings and cache")
        case "memory": return i18n.tr("What Steam left in the phone's memory")
        }
        return key
    }

    function stepTitle(name) {
        switch (name) {
        case "stop": return i18n.tr("Stopping Steam (after any Steam Cloud upload) and UTDeck's helpers")
        case "steam": return i18n.tr("Steam's files")
        case "utdeck": return i18n.tr("UTDeck's files")
        case "entries": return i18n.tr("App drawer entries")
        case "memory": return i18n.tr("Memory")
        case "appdata": return i18n.tr("UTDeck's settings (once the app is closed)")
        case "app": return i18n.tr("The app itself (once it is closed)")
        }
        return name
    }

    function total(action) {
        let kb = 0
        for (const i of uninstall.items)
            if (i.action === action)
                kb += i.kb
        return kb
    }

    function sizeOf(key) {
        for (const i of uninstall.items)
            if (i.key === key)
                return i.kb
        return 0
    }

    readonly property bool refused: {
        for (const i of uninstall.items)
            if (i.action === "refuse")
                return true
        return false
    }

    function replan() {
        page.confirming = false
        uninstall.plan(deleteGames.checked, deleteSaves.checked)
    }

    Component.onCompleted: page.replan()

    // Once it runs, the page stays: what it removes is what the other pages run on.
    Keys.onEscapePressed: (event) => {
        event.accepted = page.locked
    }

    header: PageHeader {
        id: pageHeader

        title: i18n.tr("Uninstall UTDeck")
        leadingActionBar.visible: !page.locked
    }

    Flickable {
        anchors {
            top: pageHeader.bottom
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        clip: true
        contentHeight: column.height + units.gu(4)

        Column {
            id: column

            x: units.gu(2)
            y: units.gu(2)
            width: parent.width - units.gu(4)
            spacing: units.gu(1.5)

            // --- Before: what goes, what stays ---
            Column {
                width: parent.width
                spacing: units.gu(1.5)
                visible: !uninstall.started

                Label {
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: i18n.tr("Removes from this phone what UTDeck put on it: its programs, Steam's program and your Steam sign-in. Your games and saved games stay unless you tick them below; installing UTDeck again finds them.")
                }

                Label {
                    width: parent.width
                    visible: page.refused
                    wrapMode: Text.Wrap
                    color: theme.palette.normal.negative
                    text: i18n.tr("This is UTDeck's development phone (utdeck-probe/DEVELOPMENT): the list shows what would go, but nothing is removed here.")
                }

                Row {
                    spacing: units.gu(1)
                    visible: uninstall.planning

                    ActivityIndicator {
                        running: uninstall.planning
                    }

                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        text: i18n.tr("Measuring...")
                    }
                }

                Label {
                    text: i18n.tr("Removed: %1").arg(page.size(page.total("remove")))
                    textSize: Label.Large
                }

                Repeater {
                    model: uninstall.items.filter((i) => i.action === "remove")

                    delegate: Label {
                        required property var modelData

                        width: column.width
                        wrapMode: Text.Wrap
                        text: i18n.tr("%1: %2").arg(page.what(modelData.key)).arg(page.size(modelData.kb))
                    }
                }

                Label {
                    visible: page.total("keep") > 0
                    text: i18n.tr("Kept: %1").arg(page.size(page.total("keep")))
                    textSize: Label.Large
                }

                Repeater {
                    model: uninstall.items.filter((i) => i.action === "keep")

                    delegate: Label {
                        required property var modelData

                        width: column.width
                        wrapMode: Text.Wrap
                        text: i18n.tr("%1: %2").arg(page.what(modelData.key)).arg(page.size(modelData.kb))
                    }
                }

                Row {
                    spacing: units.gu(1.5)

                    CheckBox {
                        id: deleteGames

                        onCheckedChanged: page.replan()
                    }

                    Label {
                        width: column.width - units.gu(5)
                        anchors.verticalCenter: parent.verticalCenter
                        wrapMode: Text.Wrap
                        text: i18n.tr("Also delete the Steam games and the game folders you copied (%1)")
                              .arg(page.size(page.sizeOf("games") + page.sizeOf("copied")))
                    }
                }

                Row {
                    spacing: units.gu(1.5)

                    CheckBox {
                        id: deleteSaves

                        onCheckedChanged: page.replan()
                    }

                    Label {
                        width: column.width - units.gu(5)
                        anchors.verticalCenter: parent.verticalCenter
                        wrapMode: Text.Wrap
                        text: i18n.tr("Also delete saved games and game settings (%1). Saves in Steam Cloud stay in the cloud; a game you installed into its own Windows folder goes with them.")
                              .arg(page.size(page.sizeOf("saves") + page.sizeOf("own")))
                    }
                }

                Row {
                    spacing: units.gu(1.5)

                    CheckBox {
                        id: removeApp

                        checked: true
                    }

                    Label {
                        width: column.width - units.gu(5)
                        anchors.verticalCenter: parent.verticalCenter
                        wrapMode: Text.Wrap
                        text: i18n.tr("Also remove this app when it closes")
                    }
                }

                Button {
                    visible: !page.confirming
                    text: i18n.tr("Uninstall...")
                    color: theme.palette.normal.negative
                    enabled: !uninstall.planning && !page.refused && library.runningGame === ""
                    onClicked: page.confirming = true
                }

                Label {
                    width: parent.width
                    visible: library.runningGame !== ""
                    wrapMode: Text.Wrap
                    color: theme.palette.normal.backgroundSecondaryText
                    text: i18n.tr("A game is running: quit it first.")
                }

                Column {
                    width: parent.width
                    spacing: units.gu(1.5)
                    visible: page.confirming

                    Label {
                        width: parent.width
                        wrapMode: Text.Wrap
                        text: i18n.tr("%1 is deleted, and this cannot be undone.").arg(page.size(page.total("remove")))
                    }

                    Row {
                        spacing: units.gu(1.5)

                        Button {
                            text: i18n.tr("Uninstall now")
                            color: theme.palette.normal.negative
                            onClicked: {
                                if (uninstall.start(deleteGames.checked, deleteSaves.checked, removeApp.checked))
                                    page.forceActiveFocus()
                            }
                        }

                        Button {
                            text: i18n.tr("Cancel")
                            onClicked: page.confirming = false
                        }
                    }
                }
            }

            // --- While it runs, and after ---
            Repeater {
                model: uninstall.started ? uninstall.steps : []

                delegate: Item {
                    required property var modelData

                    width: column.width
                    height: units.gu(6)

                    ActivityIndicator {
                        id: busy

                        anchors.verticalCenter: parent.verticalCenter
                        width: units.gu(3)
                        height: width
                        running: modelData.state === "running"
                        visible: running
                    }

                    Icon {
                        anchors.centerIn: busy
                        width: units.gu(2.5)
                        height: width
                        visible: !busy.running
                        name: modelData.state === "done" ? "tick"
                              : modelData.state === "failed" ? "dialog-warning-symbolic"
                              : "media-record"
                        color: modelData.state === "done" ? theme.palette.normal.positive
                               : modelData.state === "failed" ? theme.palette.normal.negative
                               : theme.palette.normal.base
                    }

                    Column {
                        anchors {
                            left: busy.right
                            leftMargin: units.gu(2)
                            right: parent.right
                            verticalCenter: parent.verticalCenter
                        }

                        Label {
                            width: parent.width
                            text: page.stepTitle(modelData.name)
                            elide: Text.ElideRight
                        }

                        Label {
                            width: parent.width
                            visible: text !== ""
                            text: modelData.detail
                            textSize: Label.Small
                            elide: Text.ElideRight
                            color: modelData.state === "failed" ? theme.palette.normal.negative
                                                                : theme.palette.normal.backgroundSecondaryText
                        }
                    }
                }
            }

            Label {
                width: parent.width
                visible: uninstall.lastError !== ""
                wrapMode: Text.Wrap
                color: theme.palette.normal.negative
                text: uninstall.lastError
            }

            Label {
                width: parent.width
                visible: uninstall.started && uninstall.failed
                wrapMode: Text.Wrap
                text: i18n.tr("The uninstall stopped (the step above says why). What was removed before stays removed; you can go back and try again.")
            }

            Column {
                width: parent.width
                spacing: units.gu(1.5)
                visible: uninstall.finished && !uninstall.failed

                Label {
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: page.total("keep") > 0
                          ? i18n.tr("UTDeck is uninstalled. Kept on the phone (%1): installing UTDeck again finds them.").arg(page.size(page.total("keep")))
                          : i18n.tr("UTDeck is uninstalled.")
                }

                Repeater {
                    model: uninstall.items.filter((i) => i.action === "keep")

                    delegate: Label {
                        required property var modelData

                        width: column.width
                        wrapMode: Text.Wrap
                        textSize: Label.Small
                        color: theme.palette.normal.backgroundSecondaryText
                        text: i18n.tr("%1: %2").arg(page.what(modelData.key)).arg(modelData.where)
                    }
                }

                Label {
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: removeApp.checked
                          ? i18n.tr("Close UTDeck now: its own settings, and then the app itself, are removed once it has closed.")
                          : i18n.tr("Close UTDeck now: its own settings are removed once it has closed. The app itself stays until you remove it in the OpenStore.")
                }

                Button {
                    text: i18n.tr("Close UTDeck")
                    color: theme.palette.normal.negative
                    onClicked: Qt.quit()
                }
            }
        }
    }
}
