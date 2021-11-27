/*
 * Copyright (C) 2021 ~ 2021 Uniontech Software Technology Co., Ltd.
 *
 * Author:     liyigang<liyigang@uniontech.com>
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
#include "fileoperationsutils.h"
#include "dfm-base/base/urlroute.h"

#include <QUrl>
#include <QDebug>

#undef signals
extern "C" {
#include <gio/gio.h>
}
#define signals public

#include <sys/vfs.h>
#include <sys/stat.h>
#include <fts.h>
#include <unistd.h>
#include <sys/utsname.h>

static const int kDefaultMemoryPageSize { 4096 };

DSC_USE_NAMESPACE
DFMBASE_USE_NAMESPACE

/*!
 * \brief FileOperationsUtils::statisticsFilesSize 使用c库统计文件大小
 * 统计了所有文件的大小信息（如果文件大小 <= 0就统计这个文件大小为一个内存页，有些文件系统dir有大小，就统计dir大小，
 * 如果dir大小 <= 0就统计这个dir大小为一个内存页），统计文件的数量, 统计所有的文件及子目录路径
 * \param files 统计文件的urllist
 * \param isRecordUrl 是否统计所有的文件及子目录路径
 * \return QSharedPointer<FileOperationsUtils::FilesSizeInfo> 文件大小信息
 */
QSharedPointer<FileOperationsUtils::FilesSizeInfo> FileOperationsUtils::statisticsFilesSize(const QList<QUrl> &files, const bool &isRecordUrl)
{
    QSharedPointer<FileOperationsUtils::FilesSizeInfo> filesSizeInfo(new FilesSizeInfo);

    if (isRecordUrl)
        filesSizeInfo->allFiles.reset(new QList<QUrl>());

    for (auto url : files) {
        statisticFilesSize(url, filesSizeInfo, isRecordUrl);
    }

    filesSizeInfo->dirSize = filesSizeInfo->dirSize <= 0 ? getMemoryPageSize() : filesSizeInfo->dirSize;
    return filesSizeInfo;
}
/*!
 * \brief FileOperationsUtils::isFileInCanRemoveDevice 判断文件是否在可移除设备上
 * 注意：这里是否可以有更好的方式，判断是固态硬盘还是机械硬盘，或者是网络设备
 * \param url 文件的url
 * \return
 */
bool FileOperationsUtils::isFileInCanRemoveDevice(const QUrl &url)
{
    Q_UNUSED(url);
    QString path = url.path();
    if (path.isEmpty())
        return false;
    bool isLocal = true;
    GFile *dest_dir_file = g_file_new_for_path(path.toUtf8().constData());
    GMount *dest_dir_mount = g_file_find_enclosing_mount(dest_dir_file, nullptr, nullptr);
    if (dest_dir_mount) {
        isLocal = !g_mount_can_unmount(dest_dir_mount);
        g_object_unref(dest_dir_mount);
    }
    g_object_unref(dest_dir_file);
    return isLocal;
}
/*!
 * \brief FileOperationsUtils::getMemoryPageSize 获取当前內存页大小
 * \return 返回内存页大小
 */
quint16 FileOperationsUtils::getMemoryPageSize()
{
    static const quint16 memoryPageSize = static_cast<quint16>(getpagesize());
    return memoryPageSize > 0 ? memoryPageSize : kDefaultMemoryPageSize;
}

void FileOperationsUtils::statisticFilesSize(const QUrl &url,
                                             QSharedPointer<FileOperationsUtils::FilesSizeInfo> &sizeInfo,
                                             const bool &isRecordUrl)
{
    char *paths[2] = { nullptr, nullptr };
    paths[0] = strdup(url.path().toUtf8().toStdString().data());
    FTS *fts = fts_open(paths, 0, nullptr);
    if (paths[0])
        free(paths[0]);

    if (nullptr == fts) {
        perror("fts_open");
        qWarning() << "fts_open open error : " << QString::fromLocal8Bit(strerror(errno));
        return;
    }
    while (1) {
        FTSENT *ent = fts_read(fts);
        if (ent == nullptr) {
            break;
        }
        unsigned short flag = ent->fts_info;
        if (isRecordUrl)
            sizeInfo->allFiles->append(QUrl::fromLocalFile(ent->fts_path));
        if (flag != FTS_DP)
            sizeInfo->totalSize += ent->fts_statp->st_size <= 0 ? getMemoryPageSize() : ent->fts_statp->st_size;
        if (sizeInfo->dirSize == 0 && flag == FTS_D)
            sizeInfo->dirSize = ent->fts_statp->st_size <= 0 ? getMemoryPageSize() : static_cast<quint16>(ent->fts_statp->st_size);
        if (flag == FTS_F)
            sizeInfo->fileCount++;
    }
    fts_close(fts);
}

bool FileOperationsUtils::isAncestorUrl(const QUrl &from, const QUrl &to)
{
    QUrl parentUrl = UrlRoute::urlParent(to);
    return from.path() == parentUrl.path();
}
