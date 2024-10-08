/*
 * This file was generated by qdbusxml2cpp version 0.8
 * Command line was: qdbusxml2cpp -i ../mountcontroldbus.h -c MountControlAdapter -l MountControlDBus -a mountcontrol_adapter ../mountcontroldbus.xml
 *
 * qdbusxml2cpp is Copyright (C) 2020 The Qt Company Ltd.
 *
 * This is an auto-generated file.
 * This file may have been hand-edited. Look for HAND-EDIT comments
 * before re-generating it.
 */

#ifndef MOUNTCONTROL_ADAPTER_H
#define MOUNTCONTROL_ADAPTER_H

#include <QtCore/QObject>
#include <QtDBus/QtDBus>
#include "../mountcontroldbus.h"
QT_BEGIN_NAMESPACE
class QByteArray;
template<class T> class QList;
template<class Key, class Value> class QMap;
class QString;
class QStringList;
class QVariant;
QT_END_NAMESPACE

/*
 * Adaptor class for interface com.deepin.filemanager.daemon.MountControl
 */
class MountControlAdapter: public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.deepin.filemanager.daemon.MountControl")
    Q_CLASSINFO("D-Bus Introspection", ""
"  <interface name=\"com.deepin.filemanager.daemon.MountControl\">\n"
"    <method name=\"Mount\">\n"
"      <arg direction=\"out\" type=\"a{sv}\"/>\n"
"      <annotation value=\"QVariantMap\" name=\"org.qtproject.QtDBus.QtTypeName.Out0\"/>\n"
"      <arg direction=\"in\" type=\"s\" name=\"path\"/>\n"
"      <arg direction=\"in\" type=\"a{sv}\" name=\"opts\"/>\n"
"      <annotation value=\"QVariantMap\" name=\"org.qtproject.QtDBus.QtTypeName.In1\"/>\n"
"    </method>\n"
"    <method name=\"Unmount\">\n"
"      <arg direction=\"out\" type=\"a{sv}\"/>\n"
"      <annotation value=\"QVariantMap\" name=\"org.qtproject.QtDBus.QtTypeName.Out0\"/>\n"
"      <arg direction=\"in\" type=\"s\" name=\"path\"/>\n"
"      <arg direction=\"in\" type=\"a{sv}\" name=\"opts\"/>\n"
"      <annotation value=\"QVariantMap\" name=\"org.qtproject.QtDBus.QtTypeName.In1\"/>\n"
"    </method>\n"
"    <method name=\"SupportedFileSystems\">\n"
"      <arg direction=\"out\" type=\"as\"/>\n"
"    </method>\n"
"  </interface>\n"
        "")
public:
    MountControlAdapter(MountControlDBus *parent);
    virtual ~MountControlAdapter();

    inline MountControlDBus *parent() const
    { return static_cast<MountControlDBus *>(QObject::parent()); }

public: // PROPERTIES
public Q_SLOTS: // METHODS
    QVariantMap Mount(const QString &path, const QVariantMap &opts);
    QStringList SupportedFileSystems();
    QVariantMap Unmount(const QString &path, const QVariantMap &opts);
Q_SIGNALS: // SIGNALS
};

#endif
