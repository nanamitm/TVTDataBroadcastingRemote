import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Shapes

ApplicationWindow {
    id: window
    width: 430
    height: 850
    visible: true
    title: "TVTest Remote"
    Material.theme: Material.Dark
    Material.accent: "#5ec9c1"
    color: "#111b25"
    readonly property var selectedTarget: {
        for (const target of remote.targets) {
            if (target.id === remote.selected) return target
        }
        return null
    }

    component RemoteButton: Button {
        property string remoteKey
        Layout.fillWidth: true
        Layout.minimumHeight: 56
        enabled: remote.connected && remote.selected.length > 0
        font.pixelSize: 19
        onClicked: remote.press(remoteKey)
    }

    component DirectionButton: RemoteButton {
        id: directionButton
        property int arrowRotation: 0
        Accessible.name: text
        autoRepeat: true
        autoRepeatDelay: 450
        autoRepeatInterval: 160
        contentItem: Item {
            implicitWidth: 36
            implicitHeight: 36
            Shape {
                anchors.centerIn: parent
                width: 24
                height: 24
                rotation: directionButton.arrowRotation
                preferredRendererType: Shape.CurveRenderer
                ShapePath {
                    strokeWidth: -1
                    fillColor: directionButton.enabled ? "#f4f7fa" : "#5a6977"
                    startX: 12
                    startY: 2
                    PathLine { x: 23; y: 22 }
                    PathLine { x: 1; y: 22 }
                    PathLine { x: 12; y: 2 }
                }
            }
        }
    }

    header: ToolBar {
        Material.background: "#1c2c3b"
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 12
            Label { text: "TVTest Remote"; font.pixelSize: 21; Layout.fillWidth: true }
            ToolButton {
                id: settingsButton
                implicitWidth: 48
                implicitHeight: 48
                Accessible.name: "接続設定"
                ToolTip.text: "接続設定"
                ToolTip.visible: hovered
                onClicked: settings.open()
                contentItem: Item {
                    implicitWidth: 24
                    implicitHeight: 24
                    Item {
                        width: 24
                        height: 24
                        anchors.centerIn: parent
                        opacity: settingsButton.down ? 0.7 : 1
                        Repeater {
                            model: 8
                            delegate: Item {
                                required property int index
                                anchors.fill: parent
                                rotation: index * 45
                                Rectangle {
                                    x: 10; y: 1
                                    width: 4; height: 6
                                    radius: 0.7
                                    color: "#edf4f8"
                                    antialiasing: true
                                }
                            }
                        }
                        Rectangle {
                            anchors.centerIn: parent
                            width: 16; height: 16
                            radius: 8
                            color: "transparent"
                            border.width: 3
                            border.color: "#edf4f8"
                            antialiasing: true
                        }
                    }
                }
            }
        }
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        ColumnLayout {
            width: parent.width
            spacing: 10
            Label {
                text: window.selectedTarget && !remote.status.startsWith("操作結果:")
                      && !remote.status.startsWith("接続エラー:")
                      ? window.selectedTarget.name + " / " + window.selectedTarget.channel
                      : remote.status
                color: remote.connected ? "#88d5cc" : "#dfbc9c"
                font.pixelSize: 14
                elide: Text.ElideRight
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.topMargin: 8
                Layout.bottomMargin: 2
            }
            GridLayout {
                columns: 3
                uniformCellWidths: true
                Layout.fillWidth: true
                Layout.margins: 12
                columnSpacing: 10
                rowSpacing: 10
                RemoteButton { text: "d"; remoteKey: "data"; font.bold: true }
                DirectionButton { text: "上"; remoteKey: "up"; arrowRotation: 0 }
                RemoteButton { text: "戻る"; remoteKey: "back" }
                DirectionButton { text: "左"; remoteKey: "left"; arrowRotation: 270 }
                RemoteButton { text: "決定"; remoteKey: "enter"; Layout.minimumHeight: 72; highlighted: true }
                DirectionButton { text: "右"; remoteKey: "right"; arrowRotation: 90 }
                Item { Layout.fillWidth: true }
                DirectionButton { text: "下"; remoteKey: "down"; arrowRotation: 180 }
                Item { Layout.fillWidth: true }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                RemoteButton { text: "青"; remoteKey: "blue"; Material.background: "#255fa8" }
                RemoteButton { text: "赤"; remoteKey: "red"; Material.background: "#a53b46" }
                RemoteButton { text: "緑"; remoteKey: "green"; Material.background: "#327c55" }
                RemoteButton { text: "黄"; remoteKey: "yellow"; Material.background: "#8a6c23" }
            }
            GridLayout {
                columns: 3
                uniformCellWidths: true
                Layout.fillWidth: true
                Layout.margins: 12
                Repeater {
                    model: ["1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12"]
                    delegate: RemoteButton {
                        required property string modelData
                        text: modelData === "10" ? "10 / 0" : modelData
                        remoteKey: modelData
                    }
                }
            }
        }
    }

    Dialog {
        id: settings
        property string pendingTarget: ""
        anchors.centerIn: parent
        width: Math.min(window.width - 24, 430)
        height: Math.min(window.height - 32, implicitHeight)
        title: "接続設定"
        modal: true
        onClosed: pendingTarget = ""
        header: Label { text: settings.title; color: "#edf4f8"; font.pixelSize: 20; padding: 18 }
        footer: DialogButtonBox {
            background: Rectangle { color: "#1d2936"; radius: 16 }
            Button { text: "閉じる"; flat: true; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
            onRejected: settings.close()
        }
        background: Rectangle { color: "#1d2936"; radius: 16 }
        Overlay.modal: Rectangle { color: "#b3000000" }
        contentItem: ScrollView {
            implicitHeight: connectionForm.implicitHeight
            contentWidth: availableWidth
            ColumnLayout {
                id: connectionForm
                width: parent.width
                spacing: 12
                Label { text: "サーバーURL" }
                TextField { id: url; text: remote.savedUrl; placeholderText: "ws://192.168.1.10:18765"; Layout.fillWidth: true; inputMethodHints: Qt.ImhUrlCharactersOnly }
                RowLayout {
                    Layout.fillWidth: true
                    Button {
                        text: remote.connected ? "再接続" : "接続"
                        Layout.fillWidth: true
                        onClicked: { settings.pendingTarget = ""; remote.connectTo(url.text) }
                    }
                    Button {
                        text: "切断"
                        Layout.fillWidth: true
                        onClicked: { settings.pendingTarget = ""; remote.disconnectFromServer() }
                    }
                }
                Label {
                    text: remote.status
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                    color: remote.connected ? "#88d5cc" : "#dfbc9c"
                    font.pixelSize: 13
                }
                Rectangle { visible: remote.connected; Layout.fillWidth: true; height: 1; color: "#344657" }
                Label { visible: remote.connected; text: "操作するTVTest"; font.bold: true }
                Repeater {
                    model: remote.targets
                    delegate: ItemDelegate {
                        id: targetChoice
                        required property var modelData
                        Layout.fillWidth: true
                        highlighted: modelData.id === remote.selected
                        enabled: !modelData.busy || modelData.id === remote.selected
                        Accessible.name: modelData.name + " / " + modelData.channel
                        onClicked: {
                            if (modelData.id === remote.selected) settings.close()
                            else {
                                settings.pendingTarget = modelData.id
                                remote.claim(modelData.id)
                            }
                        }
                        contentItem: RowLayout {
                            spacing: 10
                            Rectangle {
                                Layout.alignment: Qt.AlignVCenter
                                width: 18; height: 18; radius: 9
                                color: "transparent"
                                border.width: 2
                                border.color: targetChoice.modelData.id === remote.selected ? "#5ec9c1" : "#a9b5c2"
                                Rectangle {
                                    anchors.centerIn: parent
                                    width: 8; height: 8; radius: 4
                                    color: "#5ec9c1"
                                    visible: targetChoice.modelData.id === remote.selected
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Label { text: targetChoice.modelData.name; Layout.fillWidth: true; elide: Text.ElideRight }
                                Label {
                                    text: targetChoice.modelData.channel
                                          + (targetChoice.modelData.busy && targetChoice.modelData.id !== remote.selected ? "（使用中）" : "")
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                    font.pixelSize: 13
                                    color: "#a9b5c2"
                                }
                            }
                        }
                    }
                }
                Label {
                    visible: remote.connected && remote.targets.length === 0
                    text: "TVTestの接続を待っています"
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                    color: "#a9b5c2"
                }
                Button {
                    visible: remote.selected.length > 0
                    text: "操作権を解放"
                    Layout.fillWidth: true
                    onClicked: { settings.pendingTarget = ""; remote.release() }
                }
            }
        }
    }
    Connections {
        target: remote
        function onChanged() {
            if (settings.pendingTarget.length > 0 && remote.selected === settings.pendingTarget) {
                settings.pendingTarget = ""
                settings.close()
            }
        }
    }
    Component.onCompleted: settings.open()
}
