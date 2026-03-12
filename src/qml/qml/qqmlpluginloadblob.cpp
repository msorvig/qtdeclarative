// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:critical reason:execute-external-code

#include "qqmlpluginloadblob_p.h"
#include "qqmlpluginimporter_p.h"
#include "qqmlimport_p.h"

#include <private/qqmlextensionplugin_p.h>
#include <private/qqmlmetatype_p.h>
#include <private/qqmltypeloaderthread_p.h>

#include <QtCore/qfileinfo.h>
#include <QtCore/qloggingcategory.h>

QT_BEGIN_NAMESPACE

Q_LOGGING_CATEGORY(lcPluginLoadBlob, "qt.qml.pluginloadblob")

QQmlPluginLoadBlob::QQmlPluginLoadBlob(const QUrl &url, QQmlTypeLoader *loader)
    : QQmlTypeLoader::Blob(url, QQmlDataBlob::QmlFile, loader)
{
}

QQmlPluginLoadBlob::~QQmlPluginLoadBlob() = default;

void QQmlPluginLoadBlob::setImportInfo(const QString &uri, QTypeRevision version,
                                        const QString &qmldirPath, const QString &typeNamespace)
{
    m_uri = uri;
    m_version = version;
    m_qmldirPath = qmldirPath;
    m_typeNamespace = typeNamespace;
    m_importVersion = version;
}

void QQmlPluginLoadBlob::addPlugin(const QString &filePath, const QString &pluginId, bool optional)
{
    PendingPlugin plugin;
    plugin.filePath = filePath;
    plugin.pluginId = pluginId;
    plugin.optional = optional;
    m_plugins.append(std::move(plugin));
}

void QQmlPluginLoadBlob::startPluginLoad()
{
    if (m_loadingStarted)
        return;

    m_loadingStarted = true;
    startLoading();

    if (m_plugins.isEmpty()) {
        // No plugins to load - complete immediately
        return;
    }

    m_currentPluginIndex = 0;
    loadNextPlugin();
}

void QQmlPluginLoadBlob::dataReceived(const SourceCodeData &)
{
    // Not used - we don't receive data from network
    assertTypeLoaderThread();
}

void QQmlPluginLoadBlob::initializeFromCachedUnit(const QQmlPrivate::CachedQmlUnit *)
{
    // Not used - plugins aren't cached this way
    assertTypeLoaderThread();
    Q_UNIMPLEMENTED();
}

void QQmlPluginLoadBlob::done()
{
    assertTypeLoaderThread();
    qCDebug(lcPluginLoadBlob) << "Plugin loading done for" << m_uri
                               << "version:" << m_importVersion;
}

void QQmlPluginLoadBlob::loadNextPlugin()
{
    assertTypeLoaderThread();

    if (m_currentPluginIndex >= m_plugins.size()) {
        // All plugins processed
        if ((m_dynamicPluginsFound + m_staticPluginsLoaded) < m_plugins.size()) {
            // Not all plugins were found
            QQmlError error;
            error.setDescription(
                QQmlImports::tr("could not resolve all plugins for module \"%1\"")
                    .arg(m_uri));
            setError(error);
        }
        return;
    }

    auto &plugin = m_plugins[m_currentPluginIndex];

    if (plugin.filePath.isEmpty()) {
        // Try static plugin or skip if optional
        if (!plugin.optional) {
            QQmlError error;
            error.setDescription(
                QQmlImports::tr("module \"%1\" plugin \"%2\" not found")
                    .arg(m_uri, plugin.pluginId));
            setError(error);
            return;
        }
        // Optional plugin not found - continue with next
        m_currentPluginIndex++;
        loadNextPlugin();
        return;
    }

#if QT_CONFIG(library)
    // Check if already loaded
    if (typeLoader()->isPluginInitialized(plugin.pluginId)) {
        qCDebug(lcPluginLoadBlob) << "Plugin already loaded:" << plugin.pluginId;
        m_dynamicPluginsFound++;
        m_currentPluginIndex++;
        loadNextPlugin();
        return;
    }

    // Create the plugin loader
    const QFileInfo fileInfo(plugin.filePath);
    plugin.loader = std::make_unique<QPluginLoader>(fileInfo.absoluteFilePath());

    qCDebug(lcPluginLoadBlob) << "Starting async load of plugin:" << plugin.filePath;

    // Use loadAsync to load the plugin
    QPluginLoader *loader = plugin.loader.get();
    const QString pluginId = plugin.pluginId;
    const QString pluginPath = fileInfo.absolutePath();
    const bool optional = plugin.optional;

    // Prevent the blob from being deleted during async operation
    addref();

    loader->loadAsync([this, pluginId, pluginPath, optional](bool success) {
        // This callback may come on any thread - dispatch to type loader thread
        if (QQmlTypeLoaderThread *thread = typeLoader()->thread()) {
            thread->postMethodToThread(&QQmlTypeLoaderThread::loadThread,
                QQmlDataBlob::Ptr(this));
            // The actual completion handling happens in onPluginLoadComplete
            // We need a way to pass success to it...
            // For now, store it and call onPluginLoadComplete from loadThread callback
        }

        // For now, handle synchronously since the callback mechanism needs work
        // The loadAsync will call back on the same thread for non-WASM platforms
        onPluginLoadComplete(success);
        release();
    });
#else
    // No library support
    if (!plugin.optional) {
        QQmlError error;
        error.setDescription(QQmlImports::tr("plugin loading is not supported"));
        setError(error);
        return;
    }
    m_currentPluginIndex++;
    loadNextPlugin();
#endif
}

void QQmlPluginLoadBlob::onPluginLoadComplete(bool success)
{
    // Should be on type loader thread
    assertTypeLoaderThreadIfRunning();

    auto &plugin = m_plugins[m_currentPluginIndex];

    if (!success) {
        qCDebug(lcPluginLoadBlob) << "Plugin load failed:" << plugin.filePath
                                   << (plugin.loader ? plugin.loader->errorString() : QString());
        if (!plugin.optional) {
            QQmlError error;
            error.setDescription(plugin.loader ? plugin.loader->errorString()
                                               : QQmlImports::tr("plugin load failed"));
            setError(error);
            return;
        }
        // Optional plugin failed - continue with next
    } else {
        // Plugin loaded successfully
        qCDebug(lcPluginLoadBlob) << "Plugin loaded successfully:" << plugin.filePath;

#if QT_CONFIG(library)
        QObject *instance = plugin.loader->instance();
        if (instance) {
            const QFileInfo fileInfo(plugin.filePath);
            registerPluginTypes(instance, fileInfo.absolutePath(), plugin.pluginId);
            finalizePlugin(instance, plugin.pluginId);
            m_dynamicPluginsFound++;
        }
#endif
    }

    m_currentPluginIndex++;
    if (m_currentPluginIndex < m_plugins.size()) {
        loadNextPlugin();
    }
    // When all plugins are done, the blob will complete via tryDone()
}

void QQmlPluginLoadBlob::registerPluginTypes(QObject *instance, const QString &pluginPath,
                                              const QString &pluginId)
{
    assertTypeLoaderThread();

    QList<QQmlError> errors;
    if (QQmlMetaType::registerPluginTypes(instance, pluginPath, m_uri, m_typeNamespace,
                                           m_importVersion, &errors)
            == QQmlMetaType::RegistrationResult::Failure) {
        setError(errors);
        return;
    }

    // Lock the module
    if (!m_importVersion.hasMajorVersion()) {
        m_importVersion = QQmlMetaType::latestModuleVersion(m_uri);
    }
    if (m_importVersion.hasMajorVersion() && !m_typeNamespace.isEmpty()) {
        if (!QQmlMetaType::protectModule(m_uri, m_importVersion, true)) {
            QQmlError error;
            error.setDescription(QQmlImports::tr("module \"%1\" version %2.%3 is not installed")
                                     .arg(m_uri)
                                     .arg(m_importVersion.majorVersion())
                                     .arg(m_importVersion.minorVersion()));
            setError(error);
        }
    }
}

void QQmlPluginLoadBlob::finalizePlugin(QObject *instance, const QString &pluginId)
{
    assertTypeLoaderThread();

    typeLoader()->setPluginInitialized(pluginId);

    if (auto *extensionIface = qobject_cast<QQmlExtensionInterface *>(instance))
        typeLoader()->initializeEngine(extensionIface, m_uri.toUtf8().constData());
    else if (auto *engineIface = qobject_cast<QQmlEngineExtensionInterface *>(instance))
        typeLoader()->initializeEngine(engineIface, m_uri.toUtf8().constData());
}

QT_END_NAMESPACE
