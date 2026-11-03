# Third-party notices

- Qt 6.11.1: Qt Core, Network, WebSockets, Widgets, QML, Quick, Quick Controls and supporting plugins. Qt is dynamically linked; the applicable license texts are distributed with the Qt installation and copied into `licenses/Qt` by the Windows packaging script. Obtain matching Qt sources through the Qt installer/source packages. Source changes to Qt were not made by this project. Qt's third-party notices and individual source-file licenses also apply.
- nlohmann/json 3.10.5: Copyright (c) 2013-2022 Niels Lohmann. MIT license; full text appears at the top of `third_party/nlohmann/json.hpp`. This library is used by the Qt-free plugin bridge and its native tests/demo.
- Windows MinGW builds include the compiler runtimes supplied with the selected Qt MinGW toolchain. Their upstream licenses and GCC runtime exception apply.

Development builds and packages produced here do not change or supersede the license terms of these dependencies. The independent TVTDataBroadcastingWV2 plugin retains its existing licensing and dependencies.
