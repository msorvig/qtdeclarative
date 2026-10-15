qtdeclarative (msorvig fork)
============================

This is a patched fork of qtdeclarative.

Branches named `<qt-branch>-<patch-set>` carry a single patch set on top of the
corresponding upstream branch. Combined patch sets are merged to the plain Qt
branch name (`dev`).

wasm-dynlnk
-----------

WebAssembly dynamic linking: discover and load QML module plugins on demand over
HTTP instead of preloading every plugin at startup. Requires the companion
qtbase patches on the branch of the same name.

Branches: `dev-wasm-dynlnk`

- QML: Use QPluginLoader::loadAsync for async plugin loading
- wasm: Enable on-demand async QML plugin loading via emscripten_dlopen

---

qtdeclarative
=============

The qtdeclarative repository is home to QML, Qt Quick and several related modules:

- [Qt Qml](https://doc.qt.io/qt-6/qtqml-index.html)
- [Qt Qml Core](https://doc.qt.io/qt-6/qtqmlcore-index.html)
- [Qt Qml Models](https://doc.qt.io/qt-6/qtqml-models-qmlmodule.html)
- [Qt Qml WorkerScript](https://doc.qt.io/qt-6/qml-qtqml-workerscript-workerscript.html)
- [Qt Qml XmlListModel](https://doc.qt.io/qt-6/qml-qtqml-xmllistmodel-xmllistmodel.html)
- [Qt Quick](https://doc.qt.io/qt-6/qtquick-index.html)
- [Qt Quick Controls 2](https://doc.qt.io/qt-6/qtquickcontrols-index.html)
- [Qt Quick Layouts](https://doc.qt.io/qt-6/qtquicklayouts-index.html)
- [Qt Quick Local Storage](https://doc.qt.io/qt-6/qtquick-localstorage-qmlmodule.html)
- [Qt Quick Particles](https://doc.qt.io/qt-6/qtquick-particles-qmlmodule.html)
- [Qt Quick Shapes](https://doc.qt.io/qt-6/qtquick-shapes-qmlmodule.html)
- [Qt Quick Templates](https://doc.qt.io/qt-6/qtquick-templates2-qmlmodule.html)
- [Qt Quick Test](https://doc.qt.io/qt-6/qtquicktest-index.html)

In addition, there are several Qt Labs modules:

- [Qt Labs Animation](https://doc.qt.io/qt-6/qt-labs-animation-qmlmodule.html)
- [Qt Labs FolderListModel](https://doc.qt.io/qt-6/qml-qt-labs-folderlistmodel-folderlistmodel.html)
- [Qt Labs Platform](https://doc.qt.io/qt-6/qtlabsplatform-index.html)
- [Qt Labs Models](https://doc.qt.io/qt-6/qtqml-models-qmlmodule.html)
- [Qt Labs Settings](https://doc.qt.io/qt-6/qml-qt-labs-settings-settings.html)
- [Qt Labs Shared Image Provider](https://doc.qt.io/qt-6/qt-labs-sharedimage-qmlmodule.html)
- [Qt Labs WavefrontMesh](https://doc.qt.io/qt-6/qt-labs-wavefrontmesh-qmlmodule.html)
