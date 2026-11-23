import QtQuick
import Lomiri.Components

// Stage 4.7: Steam's work for the app, on a page of its own (opened from the library's header): the
// install or uninstall asked from the app, the downloads in progress (size, speed, time left) and what
// ended while the app ran.
Page {
    id: page

    header: PageHeader {
        id: pageHeader

        title: i18n.tr("Downloads")
    }

    function size(mb) {
        return mb >= 1024 ? i18n.tr("%1 GB").arg((mb / 1024).toFixed(1)) : i18n.tr("%1 MB").arg(mb)
    }

    function duration(seconds) {
        if (seconds < 60)
            return i18n.tr("under a minute")
        if (seconds < 3600)
            return i18n.tr("%1 min").arg(Math.round(seconds / 60))
        return i18n.tr("%1 h %2 min").arg(Math.floor(seconds / 3600)).arg(Math.round((seconds % 3600) / 60))
    }

    Flickable {
        anchors {
            top: pageHeader.bottom
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        contentHeight: column.height + units.gu(4)
        clip: true

        Column {
            id: column

            x: units.gu(2)
            y: units.gu(2)
            width: parent.width - units.gu(4)
            spacing: units.gu(2)

            Label {
                width: parent.width
                visible: steamGames.lastError !== ""
                wrapMode: Text.Wrap
                color: theme.palette.normal.negative
                text: steamGames.lastError
            }

            // The install or uninstall asked from the app, until Steam has taken it.
            Row {
                width: parent.width
                spacing: units.gu(1.5)
                visible: steamGames.task.appId !== undefined

                ActivityIndicator {
                    running: parent.visible
                    anchors.verticalCenter: parent.verticalCenter
                }

                Label {
                    width: parent.width - units.gu(5)
                    anchors.verticalCenter: parent.verticalCenter
                    wrapMode: Text.Wrap
                    text: steamGames.task.verb === "install"
                          ? i18n.tr("Asking Steam to install %1 (if Steam was off, it starts first: about a minute)...").arg(steamGames.task.name)
                          : steamGames.task.verb === "validate"
                            ? i18n.tr("Steam is checking %1's files, and fetches again what is missing or damaged...").arg(steamGames.task.name)
                            : i18n.tr("Steam is uninstalling %1 (it first runs the game's own uninstall step: a minute or two)...").arg(steamGames.task.name)
                }
            }

            Label {
                width: parent.width
                visible: steamGames.downloads.length === 0 && steamGames.task.appId === undefined
                wrapMode: Text.Wrap
                color: theme.palette.normal.backgroundSecondaryText
                text: i18n.tr("Nothing is downloading. Games are installed from + (Add a game), From your Steam library.")
            }

            Label {
                width: parent.width
                visible: steamGames.downloads.length > 0
                wrapMode: Text.Wrap
                textSize: Label.Small
                color: theme.palette.normal.backgroundSecondaryText
                text: i18n.tr("The phone stays awake until the downloads end: you can lock it, and they go on.")
            }

            // Stage 4.7: Steam's default for each game ("AllowOtherDownloadsWhileRunning" 0).
            Label {
                width: parent.width
                visible: steamGames.downloads.length > 0 && library.runningGame !== ""
                wrapMode: Text.Wrap
                textSize: Label.Small
                color: theme.palette.normal.backgroundSecondaryText
                text: i18n.tr("While a game runs, Steam holds other downloads back; they go on when it ends.")
            }

            Repeater {
                model: steamGames.downloads

                delegate: Column {
                    required property var modelData

                    width: column.width
                    spacing: units.gu(0.5)

                    Label {
                        width: parent.width
                        elide: Text.ElideRight
                        text: steamGames.task.verb === "validate" && steamGames.task.appId === modelData.appId
                              ? i18n.tr("%1 (repair)").arg(modelData.name)
                              : modelData.update ? i18n.tr("%1 (update)").arg(modelData.name) : modelData.name
                    }

                    ProgressBar {
                        width: parent.width
                        indeterminate: modelData.percent < 0
                        minimumValue: 0
                        maximumValue: 100
                        value: Math.max(0, modelData.percent)
                    }

                    Label {
                        width: parent.width
                        wrapMode: Text.Wrap
                        textSize: Label.Small
                        color: theme.palette.normal.backgroundSecondaryText
                        text: {
                            const parts = []
                            parts.push(modelData.totalMb > 0
                                       ? i18n.tr("%1 of %2").arg(page.size(modelData.doneMb)).arg(page.size(modelData.totalMb))
                                       : page.size(modelData.doneMb))
                            if (modelData.speedKb >= 0)
                                parts.push(modelData.speedKb >= 1024 ? i18n.tr("%1 MB/s").arg((modelData.speedKb / 1024).toFixed(1))
                                                                     : i18n.tr("%1 KB/s").arg(modelData.speedKb))
                            if (modelData.secondsLeft >= 0)
                                parts.push(i18n.tr("%1 left").arg(page.duration(modelData.secondsLeft)))
                            return parts.join(" · ")
                        }
                    }
                }
            }

            Label {
                visible: steamGames.finished.length > 0
                text: i18n.tr("Done")
                textSize: Label.Large
            }

            Repeater {
                model: steamGames.finished

                delegate: Label {
                    required property var modelData

                    width: column.width
                    wrapMode: Text.Wrap
                    text: modelData.time + "  " + (modelData.what === "installed" ? i18n.tr("%1 installed").arg(modelData.name)
                                                   : modelData.what === "uninstalledAll" ? i18n.tr("%1 uninstalled, with its Windows folder").arg(modelData.name)
                                                   : modelData.what === "checked"
                                                     ? (modelData.detail === 0 ? i18n.tr("%1: files checked, all fine").arg(modelData.name)
                                                        : modelData.detail > 0 ? i18n.tr("%1: files checked, %2 missing or damaged fetched again").arg(modelData.name).arg(modelData.detail)
                                                        : modelData.detail === -3 ? i18n.tr("%1: files not checked: Steam does not check a game that is running or starting. Try again once it has ended.").arg(modelData.name)
                                                        : i18n.tr("%1: files checked").arg(modelData.name))
                                                   : i18n.tr("%1 uninstalled").arg(modelData.name))
                }
            }
        }
    }
}
