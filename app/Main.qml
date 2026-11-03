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
            ToolButton { text: "接続設定"; onClicked: settings.open() }
        }
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        ColumnLayout {
            width: parent.width
            spacing: 10
            Label {
                text: remote.status
                color: remote.connected ? "#88d5cc" : "#dfbc9c"
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                Layout.margins: 14
            }
            Label { text: "操作先"; font.bold: true; Layout.leftMargin: 16 }
            Repeater {
                model: remote.targets
                delegate: ItemDelegate {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.leftMargin: 8
                    Layout.rightMargin: 8
                    text: (modelData.id === remote.selected ? "● " : "○ ")
                        + modelData.name + "  /  " + modelData.channel
                        + (modelData.busy && modelData.id !== remote.selected ? "（使用中）" : "")
                    highlighted: modelData.id === remote.selected
                    enabled: !modelData.busy || modelData.id === remote.selected
                    onClicked: remote.claim(modelData.id)
                }
            }
            Label {
                visible: remote.connected && remote.targets.length === 0
                text: "TVTestの接続を待っています"
                Layout.leftMargin: 16
            }
            Label {
                visible: remote.selected.length === 0
                text: "操作するTVTestを選択してください"
                color: "#a9b5c2"
                Layout.leftMargin: 16
            }
            GridLayout {
                columns: 3
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
            Button {
                text: "操作権を解放"
                enabled: remote.selected.length > 0
                Layout.alignment: Qt.AlignHCenter
                onClicked: remote.release()
            }
        }
    }

    Dialog {
        id: settings
        anchors.centerIn: parent
        width: Math.min(window.width - 24, 430)
        height: Math.min(window.height - 32, implicitHeight)
        title: "中継サーバーへ接続"
        modal: true
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
                Label { text: "同じLAN内のPCのアドレスを入力してください。"; wrapMode: Text.Wrap; Layout.fillWidth: true; color: "#a9b5c2" }
                Button {
                    text: "接続"
                    Layout.fillWidth: true
                    onClicked: { remote.connectTo(url.text); settings.close() }
                }
                Button { text: "切断"; Layout.fillWidth: true; onClicked: { remote.disconnectFromServer(); settings.close() } }
            }
        }
    }
    Component.onCompleted: settings.open()
}
