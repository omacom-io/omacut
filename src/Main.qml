import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtMultimedia
import "Format.js" as Format

ApplicationWindow {
    id: win
    width: 960
    height: 680
    minimumWidth: 640
    minimumHeight: 460
    visible: true
    title: backend.source.toString() === "" ? "omacut" : "omacut — " + fileName(backend.source)
    readonly property bool hasVideo: backend.source.toString() !== ""
    property string noticeText: ""
    readonly property string statusText: noticeText !== "" ? noticeText
        : backend.status !== "" ? backend.status
        : ""

    Material.theme: Material.Dark
    Material.accent: "#FFD60A"
    color: "#0e0e10"

    function fmt(sec) { return Format.fmt(sec); }
    function fileName(url) {
        var s = url.toString();
        return s === "" ? "" : decodeURIComponent(s.substring(s.lastIndexOf('/') + 1));
    }
    function showNotice(text) {
        noticeText = text;
        noticeTimer.restart();
    }
    function openVideo() {
        backend.openVideoDialog();
    }
    function exportVideo() {
        if (!win.hasVideo || backend.duration <= 0 || backend.busy)
            return;
        backend.exportDialog(trimBar.startSec, trimBar.endSec);
    }
    function togglePlay() {
        if (!win.hasVideo || backend.duration <= 0)
            return;
        if (player.priming)
            player.finishPriming();
        if (player.playbackState === MediaPlayer.PlayingState) {
            player.pause();
            return;
        }
        var pos = player.position / 1000;
        if (pos < trimBar.startSec || pos >= trimBar.endSec)
            player.position = Math.round(trimBar.startSec * 1000);
        player.play();
    }

    Shortcut {
        sequence: "Space"
        context: Qt.ApplicationShortcut
        enabled: win.hasVideo
        onActivated: togglePlay()
    }

    Shortcut {
        sequence: "Return"
        context: Qt.ApplicationShortcut
        enabled: win.hasVideo && backend.duration > 0 && !backend.busy
        onActivated: exportVideo()
    }

    Shortcut {
        sequence: "Enter"
        context: Qt.ApplicationShortcut
        enabled: win.hasVideo && backend.duration > 0 && !backend.busy
        onActivated: exportVideo()
    }

    Shortcut {
        sequence: "Escape"
        context: Qt.ApplicationShortcut
        onActivated: openVideo()
    }

    MediaPlayer {
        id: player
        source: backend.source
        videoOutput: videoOut
        audioOutput: AudioOutput {
            muted: player.priming
        }

        // Render the opening frame on load instead of showing black. Playback
        // starts muted and stops as soon as VideoOutput receives a frame.
        property bool primed: false
        property bool priming: false

        function startPriming() {
            if (primed || priming || backend.source.toString() === "")
                return;
            primed = true;
            priming = true;
            position = 0;
            play();
            primeFallback.restart();
        }

        function finishPriming() {
            if (!priming)
                return;
            primeFallback.stop();
            pause();
            position = 0;
            priming = false;
        }

        onMediaStatusChanged: {
            if (mediaStatus === MediaPlayer.LoadedMedia || mediaStatus === MediaPlayer.BufferedMedia)
                startPriming();
        }
        onPositionChanged: {
            if (priming && position > 0) {
                finishPriming();
                return;
            }
            // Stop at the trim end, like a clip preview.
            if (playbackState === MediaPlayer.PlayingState && position / 1000 >= trimBar.endSec) {
                pause();
                position = Math.round(trimBar.endSec * 1000);
            }
            if (!trimBar.interacting)
                trimBar.playheadSec = position / 1000;
        }
    }

    Timer {
        id: primeFallback
        interval: 250
        repeat: false
        onTriggered: player.finishPriming()
    }

    Timer {
        id: noticeTimer
        interval: 5000
        repeat: false
        onTriggered: win.noticeText = ""
    }

    component IconButton: Rectangle {
        id: iconButton
        implicitWidth: 44
        implicitHeight: 44
        radius: 22

        property string iconName: "play"
        property color iconColor: "white"
        property color buttonColor: "#2c2c2f"
        property string tipText: ""
        signal clicked()

        color: enabled ? buttonColor : "#2c2c2f"
        opacity: enabled ? 1 : 0.45

        HoverHandler { id: iconHover }
        ToolTip.visible: iconHover.hovered && tipText !== ""
        ToolTip.text: tipText

        Canvas {
            id: iconCanvas
            anchors.centerIn: parent
            width: 24
            height: 24

            onPaint: {
                var ctx = getContext("2d");
                ctx.clearRect(0, 0, width, height);
                ctx.fillStyle = iconButton.iconColor;
                ctx.strokeStyle = iconButton.iconColor;
                ctx.lineWidth = 2.4;
                ctx.lineCap = "round";
                ctx.lineJoin = "round";

                if (iconButton.iconName === "pause") {
                    ctx.fillRect(7, 5, 4, 14);
                    ctx.fillRect(14, 5, 4, 14);
                } else if (iconButton.iconName === "play") {
                    ctx.beginPath();
                    ctx.moveTo(8, 5);
                    ctx.lineTo(8, 19);
                    ctx.lineTo(19, 12);
                    ctx.closePath();
                    ctx.fill();
                } else if (iconButton.iconName === "download") {
                    ctx.beginPath();
                    ctx.moveTo(12, 4);
                    ctx.lineTo(12, 14);
                    ctx.stroke();

                    ctx.beginPath();
                    ctx.moveTo(7, 10);
                    ctx.lineTo(12, 15);
                    ctx.lineTo(17, 10);
                    ctx.stroke();

                    ctx.beginPath();
                    ctx.moveTo(6, 20);
                    ctx.lineTo(18, 20);
                    ctx.stroke();
                }
            }

            Connections {
                target: iconButton
                function onIconNameChanged() { iconCanvas.requestPaint(); }
                function onIconColorChanged() { iconCanvas.requestPaint(); }
            }
        }

        MouseArea {
            anchors.fill: parent
            enabled: iconButton.enabled
            cursorShape: Qt.PointingHandCursor
            onClicked: iconButton.clicked()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: win.hasVideo ? 16 : 0
        spacing: 14

        // --- video preview ---
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: win.hasVideo ? 12 : 0
            color: "black"
            clip: true

            VideoOutput {
                id: videoOut
                anchors.fill: parent
            }
            Connections {
                target: videoOut.videoSink
                function onVideoFrameChanged(frame) {
                    player.finishPriming();
                }
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: openVideo()
            }

            Button {
                id: openVideoButton
                anchors.centerIn: parent
                visible: !win.hasVideo
                text: "Open a video"
                highlighted: true
                focusPolicy: Qt.NoFocus
                font.pixelSize: 18
                Material.foreground: "black"
                HoverHandler {
                    cursorShape: Qt.PointingHandCursor
                }
                contentItem: Label {
                    text: openVideoButton.text
                    font: openVideoButton.font
                    color: "black"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                onClicked: openVideo()
            }
        }

        // --- timeline ---
        RowLayout {
            visible: win.hasVideo
            Layout.fillWidth: true
            spacing: 10

            IconButton {
                Layout.preferredWidth: 44
                Layout.preferredHeight: 44
                iconName: player.playbackState === MediaPlayer.PlayingState && !player.priming ? "pause" : "play"
                iconColor: "#ffffff"
                buttonColor: "#2c2c2f"
                tipText: player.playbackState === MediaPlayer.PlayingState ? "Pause" : "Play"
                enabled: backend.duration > 0
                onClicked: togglePlay()
            }

            TrimBar {
                id: trimBar
                Layout.fillWidth: true
                durationSec: backend.duration
                thumbCount: backend.thumbCount
                thumbReadyCount: backend.thumbReadyCount
                thumbRevision: backend.thumbRevision
                onScrub: (seconds) => player.position = Math.round(seconds * 1000)
            }

            IconButton {
                Layout.preferredWidth: 44
                Layout.preferredHeight: 44
                iconName: "download"
                iconColor: "#ffffff"
                buttonColor: "#2c2c2f"
                tipText: "Export"
                enabled: backend.duration > 0 && !backend.busy
                onClicked: exportVideo()
            }
        }

        // --- status line ---
        Item {
            visible: win.hasVideo
            Layout.fillWidth: true
            Layout.preferredHeight: 26

            Label {
                anchors.centerIn: parent
                width: parent.width
                visible: win.statusText !== ""
                text: win.statusText
                color: win.noticeText !== "" ? "#FFD60A" : "#b8b8bc"
                font.pixelSize: 13
                font.family: "monospace"
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideMiddle
            }

            Row {
                anchors.centerIn: parent
                visible: win.statusText === "" && backend.duration > 0 && !trimBar.trimmingRange
                spacing: 18

                Label {
                    text: fmt(trimBar.endSec - trimBar.startSec)
                    color: "#d6d6da"
                    font.pixelSize: 13
                    font.family: "monospace"
                }
            }
        }
    }

    Connections {
        target: backend
        function onInfoChanged() {
            win.noticeText = "";
            noticeTimer.stop();
            // Reset priming too, or a video opened mid-prime would stay black:
            // startPriming() bails while priming is still true.
            primeFallback.stop();
            player.priming = false;
            player.primed = false;
            trimBar.startSec = 0;
            trimBar.endSec = backend.duration;
            trimBar.playheadSec = 0;
        }
        function onExportDone(path) {
            win.showNotice("Saved " + path);
        }
        function onExportFailed(message) {
            win.showNotice("Export failed: " + message);
        }
        function onLoadError(message) {
            win.showNotice("Cannot open video: " + message);
        }
    }
}
