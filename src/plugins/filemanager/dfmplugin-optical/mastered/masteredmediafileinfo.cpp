/*
 * Copyright (C) 2022 Uniontech Software Technology Co., Ltd.
 *
 * Author:     zhangsheng<zhangsheng@uniontech.com>
 *
 * Maintainer: max-lv<lvwujun@uniontech.com>
 *             lanxuesong<lanxuesong@uniontech.com>
 *             xushitong<xushitong@uniontech.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "masteredmediafileinfo.h"
#include "masteredmediafileinfo_p.h"

#include "utils/opticalhelper.h"

#include "dfm-base/base/schemefactory.h"
#include "dfm-base/base/device/deviceproxymanager.h"
#include "dfm-base/base/device/deviceutils.h"
#include "dfm-base/interfaces/private/abstractfileinfo_p.h"
#include "dfm-base/dbusservice/global_server_defines.h"

DFMBASE_USE_NAMESPACE

namespace dfmplugin_optical {

using namespace GlobalServerDefines;

MasteredMediaFileInfo::MasteredMediaFileInfo(const QUrl &url)
    : AbstractFileInfo(url), d(new MasteredMediaFileInfoPrivate(url, this))
{
    dptr.reset(d);
    d->backupInfo(url);
}

bool MasteredMediaFileInfo::exists() const
{
    if (d->url.isEmpty()
        || !d->backerUrl.isValid()
        || d->backerUrl.isEmpty()) {
        return false;
    }
    if (d->url.fragment() == "dup") {
        return false;
    }

    return d->proxy && d->proxy->exists();
}

QString MasteredMediaFileInfo::displayOf(const DisPlayInfoType type) const
{
    if (DisPlayInfoType::kFileDisplayName == type) {
        if (OpticalHelper::burnFilePath(urlOf(UrlInfoType::kUrl)).contains(QRegularExpression("^(/*)$"))) {
            const auto &map { DevProxyMng->queryBlockInfo(d->curDevId) };
            QString idLabel { qvariant_cast<QString>(map[DeviceProperty::kIdLabel]) };
            if (idLabel.isEmpty())
                idLabel = DeviceUtils::convertSuitableDisplayName(map);
            return idLabel;
        }

        if (!d->proxy)
            return "";
        return d->proxy->displayOf(DisPlayInfoType::kFileDisplayName);
    }
    return AbstractFileInfo::displayOf(type);
}

QString MasteredMediaFileInfo::nameOf(const NameInfoType type) const
{
    switch (type) {
    case NameInfoType::kFileCopyName:
        return MasteredMediaFileInfo::displayOf(DisPlayInfoType::kFileDisplayName);
    default:
        return AbstractFileInfo::nameOf(type);
    }
}

QUrl MasteredMediaFileInfo::urlOf(const UrlInfoType type) const
{
    switch (type) {
    case FileUrlInfoType::kRedirectedFileUrl:
        if (d->proxy) {
            return d->proxy->urlOf(UrlInfoType::kUrl);
        }
        return AbstractFileInfo::urlOf(UrlInfoType::kUrl);
    case FileUrlInfoType::kParentUrl:
        return d->parentUrl();
    default:
        return AbstractFileInfo::urlOf(type);
    }
}

bool MasteredMediaFileInfo::isAttributes(const OptInfoType type) const
{
    switch (type) {
    case FileIsType::kIsDir:
        return !d->proxy || d->proxy->isAttributes(type);
    case FileIsType::kIsReadable:
        if (!d->proxy)
            return true;

        return d->proxy->isAttributes(type);
    case FileIsType::kIsWritable:
        if (!d->proxy)
            return false;
        return d->proxy->isAttributes(type);
    default:
        return AbstractFileInfo::isAttributes(type);
    }
}

QVariantHash MasteredMediaFileInfo::extraProperties() const
{
    QVariantHash ret;
    if (d->proxy) {
        ret = d->proxy->extraProperties();
    }
    ret["mm_backer"] = d->backerUrl.path();
    return ret;
}

void MasteredMediaFileInfo::refresh()
{
    AbstractFileInfo::refresh();
    if (d->proxy) {
        return;
    }

    d->backupInfo(urlOf(UrlInfoType::kUrl));
}

bool MasteredMediaFileInfo::canAttributes(const CanableInfoType type) const
{
    switch (type) {
    case FileCanType::kCanRename:
        if (d->proxy)
            return d->proxy->canAttributes(type);

        return false;
    case FileCanType::kCanRedirectionFileUrl:
        if (isAttributes(OptInfoType::kIsDir))
            return isAttributes(OptInfoType::kIsSymLink);   // fix bug 202007010021 当光驱刻录的文件夹中存在文件夹的链接时，要跳转到链接对应的目标文件夹
        return !isAttributes(OptInfoType::kIsDir);
    case FileCanType::kCanDrop:
        return d->canDrop();
    case FileCanType::kCanDragCompress:
        [[fallthrough]];
    case FileCanType::kCanHidden:
        return false;
    default:
        return AbstractFileInfo::canAttributes(type);
    }
}

Qt::DropActions MasteredMediaFileInfo::supportedOfAttributes(const SupportedType type) const
{
    if (type == SupportType::kDrop)
        if (!OpticalHelper::isBurnEnabled())
            return Qt::IgnoreAction;
    return AbstractFileInfo::supportedOfAttributes(type);
}

QString MasteredMediaFileInfo::viewOfTip(const ViewInfoType type) const
{
    if (type == ViewType::kEmptyDir)
        return QObject::tr("Folder is empty");
    return AbstractFileInfo::viewOfTip(type);
}

MasteredMediaFileInfoPrivate::MasteredMediaFileInfoPrivate(const QUrl &url, MasteredMediaFileInfo *qq)
    : AbstractFileInfoPrivate(url, qq)
{
}

MasteredMediaFileInfoPrivate::~MasteredMediaFileInfoPrivate()
{
}

void MasteredMediaFileInfoPrivate::backupInfo(const QUrl &url)
{
    if (OpticalHelper::burnDestDevice(url).length() == 0)
        return;

    if (OpticalHelper::burnIsOnDisc(url)) {
        QString &&devFile { OpticalHelper::burnDestDevice(url) };
        QString &&mnt { DeviceUtils::getMountInfo(devFile) };
        curDevId = { DeviceUtils::getBlockDeviceId(OpticalHelper::burnDestDevice(url)) };
        if (mnt.isEmpty())
            return;
        backerUrl = QUrl::fromLocalFile(mnt + OpticalHelper::burnFilePath(url));
    } else {
        backerUrl = OpticalHelper::localStagingFile(url);
    }
    proxy = InfoFactory::create<AbstractFileInfo>(backerUrl);
}

QUrl MasteredMediaFileInfoPrivate::parentUrl() const
{
    QString burnPath { OpticalHelper::burnFilePath(url) };
    if (burnPath.contains(QRegularExpression("^(/*)$"))) {
        return QUrl::fromLocalFile(QDir::homePath());
    }
    return UrlRoute::urlParent(url);
}

bool MasteredMediaFileInfoPrivate::canDrop()
{
    if (!OpticalHelper::burnIsOnDisc(backerUrl))
        return true;
    const auto &map { DevProxyMng->queryBlockInfo(curDevId) };
    quint64 avil { qvariant_cast<quint64>(map[DeviceProperty::kSizeFree]) };
    return avil > 0;
}

}
