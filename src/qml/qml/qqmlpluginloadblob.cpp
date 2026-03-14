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

    // Clear the pending blob so future importers don't add stale dependencies
    typeLoader()->clearPendingPluginLoadBlob(m_uri);
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

    qCDebug(lcPluginLoadBlob) << "loadNextPlugin: filePath=" << plugin.filePath
                               << "pluginId=" << plugin.pluginId
                               << "optional=" << plugin.optional
                               << "QT_CONFIG(library)="
#if QT_CONFIG(library)
                               << true
#else
                               << false
#endif
                               << "QT_CONFIG(shared)="
#if QT_CONFIG(shared)
                               << true;
#else
                               << false;
#endif

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
#if defined(Q_OS_WASM) && QT_CONFIG(shared)
    // On wasm with dynamic linking, keep the path relative so
    // emscripten_dlopen fetches it relative to the HTML file via HTTP, not
    // as an absolute virtual FS path.
    qCDebug(lcPluginLoadBlob) << "Using relative path for wasm dlopen:" << plugin.filePath;
    m_currentLoader = std::make_unique<QPluginLoader>(plugin.filePath);
#else
    qCDebug(lcPluginLoadBlob) << "Using absolute path:" << fileInfo.absoluteFilePath();
    m_currentLoader = std::make_unique<QPluginLoader>(fileInfo.absoluteFilePath());
#endif

    qCDebug(lcPluginLoadBlob) << "Starting async load of plugin:" << plugin.filePath
                               << "loader fileName:" << m_currentLoader->fileName();

    // Use loadAsync to load the plugin
    QPluginLoader *loader = m_currentLoader.get();
    const QString pluginId = plugin.pluginId;
    const QString pluginPath = fileInfo.absolutePath();
    const bool optional = plugin.optional;

    // Prevent the blob from being deleted during async operation
    addref();

    loader->loadAsync([this](bool success) {
        qCDebug(lcPluginLoadBlob) << "loadAsync callback: success=" << success;
        onPluginLoadComplete(success);
        release();
    });
#else
    qCDebug(lcPluginLoadBlob) << "No library support - skipping plugin";
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
                                   << (m_currentLoader ? m_currentLoader->errorString() : QString());
        if (!plugin.optional) {
            QQmlError error;
            error.setDescription(m_currentLoader ? m_currentLoader->errorString()
                                               : QQmlImports::tr("plugin load failed"));
            setError(error);
            return;
        }
        // Optional plugin failed - continue with next
    } else {
        // Plugin loaded successfully
        qCDebug(lcPluginLoadBlob) << "Plugin loaded successfully:" << plugin.filePath;

#if QT_CONFIG(library)
        QObject *instance = m_currentLoader->instance();
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
    } else {
        // All plugins processed — transition out of Loading so tryDone() can complete
        setStatus(QQmlDataBlob::ResolvingDependencies);
        tryDone();
    }
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
