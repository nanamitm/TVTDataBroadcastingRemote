import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

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
                RemoteButton { text: "▲"; remoteKey: "up"; autoRepeat: true; autoRepeatDelay: 450; autoRepeatInterval: 160 }
                RemoteButton { text: "戻る"; remoteKey: "back" }
                RemoteButton { text: "◀"; remoteKey: "left"; autoRepeat: true; autoRepeatDelay: 450; autoRepeatInterval: 160 }
                RemoteButton { text: "決定"; remoteKey: "enter"; Layout.minimumHeight: 72; highlighted: true }
                RemoteButton { text: "▶"; remoteKey: "right"; autoRepeat: true; autoRepeatDelay: 450; autoRepeatInterval: 160 }
                Item { Layout.fillWidth: true }
                RemoteButton { text: "▼"; remoteKey: "down"; autoRepeat: true; autoRepeatDelay: 450; autoRepeatInterval: 160 }
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
        height: Math.min(window.height - 32, 560)
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
        ScrollView {
            anchors.fill: parent
            contentWidth: availableWidth
            ColumnLayout {
                width: parent.width
                spacing: 12
                Label { text: "サーバーURL" }
                TextField { id: url; text: remote.savedUrl; placeholderText: "wss://192.168.1.10:18765"; Layout.fillWidth: true; inputMethodHints: Qt.ImhUrlCharactersOnly }
                Label { text: "接続トークン（保存しません）" }
                TextField { id: token; echoMode: TextInput.Password; Layout.fillWidth: true; inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhSensitiveData }
                Label { text: "証明書 SHA-256 指紋" }
                TextField { id: fingerprint; text: remote.savedFingerprint; Layout.fillWidth: true; placeholderText: "PCで確認した64桁の指紋"; inputMethodHints: Qt.ImhNoPredictiveText }
                Label { text: "PCで確認した接続情報を入力してください。バックグラウンド移行時は切断します。"; wrapMode: Text.Wrap; Layout.fillWidth: true; color: "#a9b5c2" }
                Button {
                    text: "接続"
                    Layout.fillWidth: true
                    onClicked: { remote.connectTo(url.text, token.text, fingerprint.text); token.clear(); settings.close() }
                }
                Button { text: "切断"; Layout.fillWidth: true; onClicked: { remote.disconnectFromServer(); token.clear(); settings.close() } }
            }
        }
    }
    Component.onCompleted: settings.open()
}
