// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:critical reason:execute-external-code

#ifndef QQMLPLUGINLOADBLOB_P_H
#define QQMLPLUGINLOADBLOB_P_H

//
//  W A R N I N G
//  -------------
//
// This file is not part of the Qt API.  It exists purely as an
// implementation detail.  This header file may change from version to
// version without notice, or even be removed.
//
// We mean it.
//

#include <private/qqmltypeloader_p.h>
#include <private/qqmltypeloaderqmldircontent_p.h>

#include <QtCore/qpluginloader.h>

#include <memory>

QT_BEGIN_NAMESPACE

class QQmlPluginLoadBlob : public QQmlTypeLoader::Blob
{
public:
    QQmlPluginLoadBlob(const QUrl &url, QQmlTypeLoader *loader);
    ~QQmlPluginLoadBlob() override;

    // Configuration - call before startPluginLoad()
    void setImportInfo(const QString &uri, QTypeRevision version,
                       const QString &qmldirPath, const QString &typeNamespace);
    void addPlugin(const QString &filePath, const QString &pluginId, bool optional);

    // Start the async plugin loading
    void startPluginLoad();

    // Results
    QTypeRevision importVersion() const { return m_importVersion; }

protected:
    void dataReceived(const SourceCodeData &) override;
    void initializeFromCachedUnit(const QQmlPrivate::CachedQmlUnit *) override;
    void done() override;

private:
    struct PendingPlugin {
        QString filePath;
        QString pluginId;
        bool optional = false;
        std::unique_ptr<QPluginLoader> loader;
    };

    void loadNextPlugin();
    void onPluginLoadComplete(bool success);
    void registerPluginTypes(QObject *instance, const QString &pluginPath, const QString &pluginId);
    void finalizePlugin(QObject *instance, const QString &pluginId);

    QList<PendingPlugin> m_plugins;
    int m_currentPluginIndex = 0;
    bool m_loadingStarted = false;

    // Import context
    QString m_uri;
    QTypeRevision m_version;
    QString m_qmldirPath;
    QString m_typeNamespace;
    QTypeRevision m_importVersion;

    // Tracking
    int m_dynamicPluginsFound = 0;
    int m_staticPluginsLoaded = 0;
};

QT_END_NAMESPACE

#endif // QQMLPLUGINLOADBLOB_P_H
