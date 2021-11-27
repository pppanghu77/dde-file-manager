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
#include "docopyfilesworker.h"
#include "fileoperations/fileoperationutils/fileoperationsutils.h"
#include "dfm-base/base/schemefactory.h"
#include "dfm-base/base/abstractdiriterator.h"
#include "fileoperations/fileoperationutils/statisticsfilessize.h"
#include "storageinfo.h"

#include <QUrl>
#include <QDebug>
#include <QApplication>
#include <QTime>
#include <QRegularExpression>
#include <QProcess>

#include <dfm-io/dfmio_global.h>
#include <core/diofactory.h>

#include <syscall.h>
#include <fcntl.h>
#include <zlib.h>

static const quint32 kMaxBufferLength { 1024 * 1024 * 1 };
static const quint32 kBigFileSize { 100 * 1024 * 1024 };

DSC_USE_NAMESPACE
USING_IO_NAMESPACE
DoCopyFilesWorker::DoCopyFilesWorker(QObject *parent)
    : AbstractWorker(parent)
{
}

DoCopyFilesWorker::~DoCopyFilesWorker()
{
}
/*!
 * \brief doOperateWork 处理用户的操作 不在拷贝线程执行的函数，协同类直接调用
 * \param actions 当前操作
 */
void DoCopyFilesWorker::doOperateWork(AbstractJobHandler::SupportActions actions)
{
    if (actions.testFlag(AbstractJobHandler::SupportAction::kStopAction)) {
        stop();
    } else if (actions.testFlag(AbstractJobHandler::SupportAction::kPauseAction)) {
        pause();
    } else if (actions.testFlag(AbstractJobHandler::SupportAction::kRememberAction)) {
        // TODO::
    } else if (actions.testFlag(AbstractJobHandler::SupportAction::kEnforceAction)) {
        // TODO::
    } else if (actions.testFlag(AbstractJobHandler::SupportAction::kCancelAction)) {
        // TODO::
    } else if (actions.testFlag(AbstractJobHandler::SupportAction::kCoexistAction)) {
        // TODO::
    } else if (actions.testFlag(AbstractJobHandler::SupportAction::kSkipAction)) {
        // TODO::
    } else if (actions.testFlag(AbstractJobHandler::SupportAction::kMergeAction)) {
        // TODO::
    } else if (actions.testFlag(AbstractJobHandler::SupportAction::kReplaceAction)) {
        // TODO::
    } else if (actions.testFlag(AbstractJobHandler::SupportAction::kRetryAction)) {
        // TODO::
    }
}

void DoCopyFilesWorker::doWork()
{
    // 执行拷贝的业务逻辑
    if (!initArgs()) {
        endCopy();
        return;
    }
    // 统计文件总大小
    statisticsFilesSize();
    // 设置读取写入数据大小的方式
    setCountProccessType();
    // 启动统计写入数据大小计时器
    startCountProccess();
    // 执行文件拷贝
    if (!copyFiles()) {
        endCopy();
        return;
    }
    // 完成
    endCopy();
}

void DoCopyFilesWorker::stop()
{
    // ToDo::停止拷贝的业务逻辑
    if (statisticsFilesSizeJob)
        statisticsFilesSizeJob->stop();
}

void DoCopyFilesWorker::pause()
{
    // ToDo::暂停拷贝的业务逻辑
    if (currentState == AbstractJobHandler::JobState::kPauseState)
        return;
    setStat(AbstractJobHandler::JobState::kPauseState);
}

void DoCopyFilesWorker::resume()
{
    if (currentState != AbstractJobHandler::JobState::kPauseState)
        return;
    setStat(AbstractJobHandler::JobState::kRunningState);
    waitCondition.wakeAll();
}

bool DoCopyFilesWorker::initArgs()
{
    time.start();
    sourceFilesTotalSize = -1;
    currentState = AbstractJobHandler::JobState::kRunningState;

    if (sources.count() <= 0) {
        // pause and emit error msg
        doHandleErrorAndWait(QUrl(), QUrl(), AbstractJobHandler::JobErrorType::kProrogramError);
        return false;
    }
    if (!target.isValid()) {
        // pause and emit error msg
        doHandleErrorAndWait(QUrl(), target, AbstractJobHandler::JobErrorType::kProrogramError);
        return false;
    }
    targetInfo = InfoFactory::create<AbstractFileInfo>(target);
    if (!targetInfo) {
        // pause and emit error msg
        doHandleErrorAndWait(QUrl(), target, AbstractJobHandler::JobErrorType::kProrogramError);
        return false;
    }

    if (targetInfo->exists()) {
        // pause and emit error msg
        doHandleErrorAndWait(QUrl(), target, AbstractJobHandler::JobErrorType::kNonexistenceError);
        return false;
    }

    return true;
}

void DoCopyFilesWorker::statisticsFilesSize()
{
    // 判读源文件所在设备位置，执行异步或者同统计源文件大小
    isSourceFileLocal = FileOperationsUtils::isFileInCanRemoveDevice(sources.at(0));

    if (isSourceFileLocal) {
        const QSharedPointer<FileOperationsUtils::FilesSizeInfo> &fileSizeInfo =
                FileOperationsUtils::statisticsFilesSize(sources, jobFlags.testFlag(AbstractJobHandler::JobFlag::kCopyToSelf));
        allFilesList = fileSizeInfo->allFiles;
        sourceFilesTotalSize = fileSizeInfo->totalSize;
        dirSize = fileSizeInfo->dirSize;
        return;
    }

    statisticsFilesSizeJob.reset(new StatisticsFilesSize(sources));
    connect(statisticsFilesSizeJob.data(), &StatisticsFilesSize::finished, this, &DoCopyFilesWorker::onStatisticsFilesSizeFinish);
    statisticsFilesSizeJob->start();
}

void DoCopyFilesWorker::setCountProccessType()
{
    // 检查目标文件的有效性
    // 判读目标文件的位置（在可移除设备并且不是ext系列的设备上使用读取写入设备大小，
    // 其他都是读取当前线程写入磁盘的数据，如果采用多线程拷贝就自行统计）

    if (!targetStorageInfo)
        targetStorageInfo.reset(new StorageInfo(target.path()));
    targetRootPath = targetStorageInfo->rootPath();
    QString rootpath = targetRootPath;

    qDebug("Target block device: \"%s\", Root Path: \"%s\"",
           targetStorageInfo->device().constData(), qPrintable(targetRootPath));

    if (targetStorageInfo->isLocalDevice()) {
        bool isFileSystemTypeExt = targetStorageInfo->fileSystemType().startsWith("ext");

        if (!isFileSystemTypeExt) {
            countWriteType = CountWriteSizeType::kWriteBlockType;
            const QByteArray dev_path = targetStorageInfo->device();

            QProcess process;

            process.start("lsblk", { "-niro", "MAJ:MIN,HOTPLUG,LOG-SEC", dev_path }, QIODevice::ReadOnly);

            if (process.waitForFinished(3000)) {
                if (process.exitCode() == 0) {
                    const QByteArray &data = process.readAllStandardOutput();
                    const QByteArrayList &list = data.split(' ');

                    qDebug("lsblk result data: \"%s\"", data.constData());

                    if (list.size() == 3) {
                        targetSysDevPath = "/sys/dev/block/" + list.first();
                        targetIsRemovable = list.at(1) == "1";

                        bool ok = false;
                        targetLogSecionSize = static_cast<qint16>(list.at(2).toInt(&ok));

                        if (!ok) {
                            targetLogSecionSize = 512;

                            qWarning() << "get target log secion size failed!";
                        }

                        if (targetIsRemovable) {
                            targetDeviceStartSectorsWritten = getSectorsWritten();
                        }

                        qDebug("Block device path: \"%s\", Sys dev path: \"%s\", Is removable: %d, Log-Sec: %d",
                               qPrintable(dev_path), qPrintable(targetSysDevPath), bool(targetIsRemovable), targetLogSecionSize);
                    } else {
                        qWarning("Failed on parse the lsblk result data, data: \"%s\"", data.constData());
                    }
                } else {
                    qWarning("Failed on exec lsblk command, exit code: %d, error message: \"%s\"", process.exitCode(), process.readAllStandardError().constData());
                }
            }
        }
        isTargetFileLocal = targetIsRemovable;
        qDebug("targetIsRemovable = %d", bool(targetIsRemovable));
    }
    // no error handling
    // this syscall has existed since Linux 2.4.11 and cannot fail
    copyTid = countWriteType == CountWriteSizeType::kTidType ? syscall(SYS_gettid) : -1;
}

void DoCopyFilesWorker::startCountProccess()
{
    if (updateProccessTimer)
        updateProccessTimer.reset(new UpdateProccessTimer());
    updateProccessTimer->moveToThread(&updateProccessThread);
    updateProccessThread.start();
    connect(this, &DoCopyFilesWorker::startUpdateProccessTimer, updateProccessTimer.data(), &UpdateProccessTimer::doStartTime);
    connect(updateProccessTimer.data(), &UpdateProccessTimer::updateProccessNotify, this, &DoCopyFilesWorker::onUpdateProccess);
    emit startUpdateProccessTimer();
}

bool DoCopyFilesWorker::copyFiles()
{
    bool reslut = false;
    for (const auto &url : sources) {
        if (!stateCheck()) {
            return false;
        }
        const auto &fileInfo = InfoFactory::create<AbstractFileInfo>(url);
        if (!fileInfo) {
            // pause and emit error msg
            if (AbstractJobHandler::SupportAction::kSkipAction != doHandleErrorAndWait(url, target, AbstractJobHandler::JobErrorType::kProrogramError)) {
                return false;
            } else {
                continue;
            }
        }
        if (!targetInfo) {
            // pause and emit error msg
            doHandleErrorAndWait(QUrl(), target, AbstractJobHandler::JobErrorType::kProrogramError);
            return false;
        }
        if (!doCopyFile(fileInfo, targetInfo)) {
            return false;
        }
    }
    return reslut;
}

bool DoCopyFilesWorker::doCopyFile(const AbstractFileInfoPointer &fromInfo, const AbstractFileInfoPointer &toInfo)
{
    AbstractFileInfoPointer newTargetInfo(nullptr);
    if (!doCheckFile(fromInfo, toInfo, newTargetInfo))
        return false;

    bool reslut = false;
    if (fromInfo->isSymLink()) {
        reslut = creatSystemLink(fromInfo, newTargetInfo);
    } else if (fromInfo->isDir()) {
        reslut = checkAndcopyDir(fromInfo, newTargetInfo);
    } else {
        reslut = checkAndCopyFile(fromInfo, newTargetInfo);
    }
    return reslut;
}

bool DoCopyFilesWorker::doCheckFile(const AbstractFileInfoPointer &fromInfo, const AbstractFileInfoPointer &toInfo, AbstractFileInfoPointer &newTargetInfo)
{
    // 检查源文件的文件信息
    if (!fromInfo) {
        return AbstractJobHandler::SupportAction::kSkipAction != doHandleErrorAndWait(QUrl(), toInfo == nullptr ? QUrl() : toInfo->url(), AbstractJobHandler::JobErrorType::kProrogramError);
    }
    // 检查源文件是否存在
    if (!fromInfo->exists()) {
        AbstractJobHandler::JobErrorType errortype = (fromInfo->path().startsWith("/root/") && !fromInfo->path().startsWith("/root/")) ? AbstractJobHandler::JobErrorType::kPermissionError : AbstractJobHandler::JobErrorType::kNonexistenceError;
        return AbstractJobHandler::SupportAction::kSkipAction == doHandleErrorAndWait(fromInfo->url(), toInfo == nullptr ? QUrl() : toInfo->url(), errortype);
    }
    // 检查目标文件的文件信息
    if (!toInfo) {
        doHandleErrorAndWait(fromInfo->url(), QUrl(), AbstractJobHandler::JobErrorType::kProrogramError);
        return false;
    }
    // 检查目标文件是否存在
    if (!toInfo->exists()) {
        AbstractJobHandler::JobErrorType errortype = (fromInfo->path().startsWith("/root/") && !fromInfo->path().startsWith("/root/")) ? AbstractJobHandler::JobErrorType::kPermissionError : AbstractJobHandler::JobErrorType::kNonexistenceError;
        doHandleErrorAndWait(fromInfo->url(), toInfo->url(), errortype);
        return false;
    }
    // 特殊文件判断
    switch (fromInfo->fileType()) {
    case AbstractFileInfo::kCharDevice:
    case AbstractFileInfo::kBlockDevice:
    case AbstractFileInfo::kFIFOFile:
    case AbstractFileInfo::kSocketFile: {
        skipWritSize += fromInfo->size() <= 0 ? dirSize : fromInfo->size();
        return AbstractJobHandler::SupportAction::kSkipAction == doHandleErrorAndWait(fromInfo->url(), toInfo->url(), AbstractJobHandler::JobErrorType::kSpecialFileError);
    }
    default:
        break;
    }

    // 创建新的目标文件并做检查
    QString fileNewName = fromInfo->fileName();
    newTargetInfo.reset(nullptr);
    if (!doCheckNewFile(fromInfo, toInfo, newTargetInfo, fileNewName, true))
        return false;

    return true;
}

bool DoCopyFilesWorker::doCheckNewFile(const AbstractFileInfoPointer &fromInfo, const AbstractFileInfoPointer &toInfo, AbstractFileInfoPointer &newTargetInfo, QString &fileNewName, bool isCountSize)
{
    fileNewName = formatFileName(fileNewName);
    // 创建文件的名称
    QUrl newTargetUrl = toInfo->url();
    QString newPath = newTargetUrl.path().endsWith("/") ? newTargetUrl.path() + fileNewName : newTargetUrl.path() + "/" + fileNewName;

    newTargetUrl.setPath(newPath);
    newTargetInfo.reset(nullptr);
    newTargetInfo = InfoFactory::create<AbstractFileInfo>(newTargetUrl);
    if (!newTargetInfo) {
        skipWritSize += isCountSize && (fromInfo->isSymLink() || fromInfo->size() <= 0) ? dirSize : fromInfo->size();
        return AbstractJobHandler::SupportAction::kSkipAction != doHandleErrorAndWait(fromInfo->url(), toInfo->url(), AbstractJobHandler::JobErrorType::kProrogramError);
    }

    if (newTargetInfo->exists()) {
        if (!jobFlags.testFlag(AbstractJobHandler::JobFlag::kCopyToSelf) && FileOperationsUtils::isAncestorUrl(fromInfo->url(), newTargetUrl)) {
            AbstractJobHandler::SupportAction action = doHandleErrorAndWait(fromInfo->url(), toInfo->url(), AbstractJobHandler::JobErrorType::kTargetIsSelfError);
            if (AbstractJobHandler::SupportAction::kSkipAction == action) {
                skipWritSize += isCountSize && (fromInfo->isSymLink() || fromInfo->size() <= 0) ? dirSize : fromInfo->size();
                return true;
            }

            if (action != AbstractJobHandler::SupportAction::kEnforceAction) {
                skipWritSize += isCountSize && (fromInfo->isSymLink() || fromInfo->size() <= 0) ? dirSize : fromInfo->size();
                return false;
            }
        };
        bool fromIsFile = fromInfo->isFile() || fromInfo->isSymLink();
        bool newTargetIsFile = newTargetInfo->isFile() || newTargetInfo->isSymLink();
        AbstractJobHandler::JobErrorType errortype = newTargetIsFile ? AbstractJobHandler::JobErrorType::kFileExistsError : AbstractJobHandler::JobErrorType::kDirectoryExistsError;
        switch (doHandleErrorAndWait(fromInfo->url(), toInfo->url(), errortype)) {
        case AbstractJobHandler::SupportAction::kReplaceAction:
            if (newTargetUrl == fromInfo->url()) {
                skipWritSize += isCountSize && (fromInfo->isSymLink() || fromInfo->size() <= 0) ? dirSize : fromInfo->size();
                return true;
            }

            if (fromIsFile && fromIsFile == newTargetIsFile) {
                break;
            } else {
                // TODO:: something is doing here
                return false;
            }
        case AbstractJobHandler::SupportAction::kMergeAction:
            if (!fromIsFile && fromIsFile == newTargetIsFile) {
                break;
            } else {
                // TODO:: something is doing here
                return false;
            }
        case AbstractJobHandler::SupportAction::kSkipAction:
            skipWritSize += isCountSize && (fromInfo->isSymLink() || fromInfo->size() <= 0) ? dirSize : fromInfo->size();
            return true;
        case AbstractJobHandler::SupportAction::kCoexistAction:
            fileNewName = getNonExistFileName(fromInfo, targetInfo);
            return doCheckNewFile(fromInfo, toInfo, newTargetInfo, fileNewName);
        default:
            return false;
        }

        return true;
    }

    return true;
}

bool DoCopyFilesWorker::doCheckFileFreeSpace(const qint64 &size)
{
    if (!targetStorageInfo) {
        targetStorageInfo.reset(new StorageInfo(target.path()));
    } else {
        targetStorageInfo->refresh();
    }
    if (targetStorageInfo->bytesTotal() <= 0) {
        return false;
    }
    return targetStorageInfo->bytesAvailable() >= size;
}

bool DoCopyFilesWorker::creatSystemLink(const AbstractFileInfoPointer &fromInfo,
                                        const AbstractFileInfoPointer &toInfo)
{
    // 创建链接文件
    skipWritSize += dirSize;
    AbstractFileInfoPointer newFromInfo = fromInfo;
    if (jobFlags.testFlag(AbstractJobHandler::JobFlag::kCopyFollowSymlink)) {
        do {
            QUrl newUrl = newFromInfo->url();
            newUrl.setPath(newFromInfo->symLinkTarget());
            const AbstractFileInfoPointer &symlinkTarget = InfoFactory::create<AbstractFileInfo>(newUrl);

            if (!symlinkTarget || !symlinkTarget->exists()) {
                break;
            }

            newFromInfo = symlinkTarget;
        } while (newFromInfo->isSymLink());

        if (newFromInfo->exists()) {
            // copy file here
            if (fromInfo->isFile()) {
                return checkAndCopyFile(fromInfo, toInfo);
            } else {
                return checkAndcopyDir(fromInfo, toInfo);
            }
        }
    }

    AbstractJobHandler::SupportAction action = AbstractJobHandler::SupportAction::kNoAction;

    do {
        if (handler.createSystemLink(toInfo->url(), newFromInfo->url())) {
            return true;
        }
        action = doHandleErrorAndWait(fromInfo->url(), toInfo->url(), AbstractJobHandler::JobErrorType::kSymlinkError, QString(QObject::tr("Fail to create symlink, cause: %1")).arg(handler.errorString()));
    } while (action == AbstractJobHandler::SupportAction::kRetryAction);
    return action == AbstractJobHandler::SupportAction::kSkipAction;
}

bool DoCopyFilesWorker::checkAndCopyFile(const AbstractFileInfoPointer fromInfo, const AbstractFileInfoPointer toInfo)
{
    AbstractJobHandler::SupportAction action = AbstractJobHandler::SupportAction::kNoAction;
    while (doCheckFileFreeSpace(fromInfo->size())) {
        action = doHandleErrorAndWait(fromInfo->url(), toInfo->url(), AbstractJobHandler::JobErrorType::kNotEnoughSpaceError);
        if (action == AbstractJobHandler::SupportAction::kRetryAction) {
            continue;
        } else if (action == AbstractJobHandler::SupportAction::kSkipAction) {
            skipWritSize += fromInfo->size() <= 0 ? dirSize : fromInfo->size();
            return true;
        }

        if (action == AbstractJobHandler::SupportAction::kEnforceAction) {
            break;
        }

        return false;
    }

    return doCopyOneFile(fromInfo, toInfo);
}

bool DoCopyFilesWorker::doCopyOneFile(const AbstractFileInfoPointer fromInfo, const AbstractFileInfoPointer toInfo)
{
    // 实现真实文件拷贝
    // do not check the args
    emitCurrentTaskNotify(fromInfo->url(), toInfo->url());
    //预先读取
    readAheadSourceFile(fromInfo);
    // 创建文件的divice
    QSharedPointer<DFile> fromDevice { nullptr }, toDevice { nullptr };
    bool reslut = false;
    if (!createFileDevices(fromInfo, toInfo, fromDevice, toDevice, reslut))
        return reslut;
    // 打开文件并创建
    if (!openFiles(fromInfo, toInfo, fromDevice, toDevice, reslut))
        return reslut;
    // 源文件大小如果为0
    if (fromInfo->size() <= 0) {
        // 对文件加权
        setTargetPermissions(fromInfo, toInfo);
        skipWritSize += dirSize;
        return true;
    }
    // resize target file
    if (jobFlags.testFlag(AbstractJobHandler::JobFlag::kCopyResizeDestinationFile) && !resizeTargetFile(fromInfo, toInfo, toDevice, reslut))
        return reslut;
    // 循环读取和写入文件，拷贝
    qint64 blockSize = fromInfo->size() > kMaxBufferLength ? kMaxBufferLength : fromInfo->size();
    char *data = new char[blockSize + 1];
    uLong sourceCheckSum = adler32(0L, nullptr, 0);
    qint64 sizeRead = 0;
    do {
        if (!doReadFile(fromInfo, toInfo, fromDevice, data, blockSize, sizeRead, reslut)) {
            delete[] data;
            data = nullptr;
            return reslut;
        }

        if (!doWriteFile(fromInfo, toInfo, toDevice, data, sizeRead, reslut)) {
            delete[] data;
            data = nullptr;
            return reslut;
        }

        if (Q_LIKELY(jobFlags.testFlag(AbstractJobHandler::JobFlag::kCopyIntegrityChecking))) {
            sourceCheckSum = adler32(sourceCheckSum, reinterpret_cast<Bytef *>(data), static_cast<uInt>(sizeRead));
        }
    } while (fromDevice->pos() == fromInfo->size());

    delete[] data;
    data = nullptr;

    // 对文件加权
    setTargetPermissions(fromInfo, toInfo);
    if (Q_UNLIKELY(!stateCheck())) {
        return false;
    }

    // 校验文件完整性
    return verifyFileIntegrity(blockSize, sourceCheckSum, fromInfo, toInfo, toDevice);
}

bool DoCopyFilesWorker::createFileDevices(const AbstractFileInfoPointer &fromInfo, const AbstractFileInfoPointer &toInfo, QSharedPointer<DFile> &fromeFile, QSharedPointer<DFile> &toFile, bool &result)
{
    if (!createFileDevice(fromInfo, toInfo, fromInfo, fromeFile, result))
        return false;
    if (!createFileDevice(fromInfo, toInfo, toInfo, toFile, result))
        return false;
    return true;
}

bool DoCopyFilesWorker::createFileDevice(const AbstractFileInfoPointer &fromInfo,
                                         const AbstractFileInfoPointer &toInfo,
                                         const AbstractFileInfoPointer &needOpenInfo,
                                         QSharedPointer<DFile> &file, bool &result)
{
    file.reset(nullptr);
    QUrl url = needOpenInfo->url();
    AbstractJobHandler::SupportAction action = AbstractJobHandler::SupportAction::kNoAction;
    QSharedPointer<DIOFactory> factory { nullptr };
    do {
        factory = produceQSharedIOFactory(url.scheme(), static_cast<QUrl>(url));
        if (!factory) {
            action = doHandleErrorAndWait(fromInfo->url(), toInfo->url(), AbstractJobHandler::JobErrorType::kDfmIoError, QObject::tr("create dfm io factory failed!"));
        }
    } while (action == AbstractJobHandler::SupportAction::kRetryAction);

    if (action != AbstractJobHandler::SupportAction::kNoAction) {
        skipWritSize += action == AbstractJobHandler::SupportAction::kSkipAction ? (fromInfo->size() <= 0 ? dirSize : fromInfo->size()) : 0;
        result = action == AbstractJobHandler::SupportAction::kSkipAction;
        return false;
    }

    do {
        file = factory->createFile();
        if (!file) {
            action = doHandleErrorAndWait(fromInfo->url(), toInfo->url(), AbstractJobHandler::JobErrorType::kDfmIoError, QObject::tr("create dfm io dfile failed!"));
        }
    } while (action == AbstractJobHandler::SupportAction::kRetryAction);

    if (action != AbstractJobHandler::SupportAction::kNoAction) {
        skipWritSize += action == AbstractJobHandler::SupportAction::kSkipAction ? (fromInfo->size() <= 0 ? dirSize : fromInfo->size()) : 0;
        result = action == AbstractJobHandler::SupportAction::kSkipAction;
        return false;
    }

    return true;
}

bool DoCopyFilesWorker::openFiles(const AbstractFileInfoPointer &fromInfo, const AbstractFileInfoPointer &toInfo, const QSharedPointer<DFile> &fromeFile, const QSharedPointer<DFile> &toFile, bool &result)
{
    if (fromInfo->size() > 0 && !openFile(fromInfo, toInfo, fromeFile, DFile::OpenFlag::ReadOnly, result)) {
        return false;
    }

    if (!openFile(fromInfo, toInfo, toFile, DFile::OpenFlag::Truncate, result)) {
        return false;
    }

    return true;
}

bool DoCopyFilesWorker::openFile(const AbstractFileInfoPointer &fromInfo,
                                 const AbstractFileInfoPointer &toInfo,
                                 const QSharedPointer<DFile> &file,
                                 const DFMIO::DFile::OpenFlag &flags,
                                 bool &result)
{
    AbstractJobHandler::SupportAction action = AbstractJobHandler::SupportAction::kNoAction;
    do {
        if (!file->open(flags)) {
            action = doHandleErrorAndWait(fromInfo->url(), toInfo->url(), AbstractJobHandler::JobErrorType::kDfmIoError, QObject::tr("create dfm io dfile failed!"));
        }
    } while (action == AbstractJobHandler::SupportAction::kRetryAction);
    if (action != AbstractJobHandler::SupportAction::kNoAction) {
        skipWritSize += action == AbstractJobHandler::SupportAction::kSkipAction ? (fromInfo->size() <= 0 ? dirSize : fromInfo->size()) : 0;
        result = action == AbstractJobHandler::SupportAction::kSkipAction;
        return false;
    }
    return true;
}

bool DoCopyFilesWorker::resizeTargetFile(const AbstractFileInfoPointer &fromInfo, const AbstractFileInfoPointer &toInfo, const QSharedPointer<DFile> &file, bool &result)
{
    AbstractJobHandler::SupportAction action = AbstractJobHandler::SupportAction::kNoAction;
    do {
        if (!file->write(QByteArray())) {
            action = doHandleErrorAndWait(fromInfo->url(), toInfo->url(), AbstractJobHandler::JobErrorType::kResizeError, QObject::tr("resize file failed!"));
        }
    } while (action == AbstractJobHandler::SupportAction::kRetryAction);
    if (action != AbstractJobHandler::SupportAction::kNoAction) {
        skipWritSize += action == AbstractJobHandler::SupportAction::kSkipAction ? (fromInfo->size() <= 0 ? dirSize : fromInfo->size()) : 0;
        result = action == AbstractJobHandler::SupportAction::kSkipAction;
        return false;
    }
    return true;
}

bool DoCopyFilesWorker::doReadFile(const AbstractFileInfoPointer &fromInfo,
                                   const AbstractFileInfoPointer &toInfo,
                                   const QSharedPointer<DFile> &fromDevice,
                                   char *data,
                                   const qint64 &blockSize, qint64 &readSize,
                                   bool &result)
{
    readSize = 0;
    qint64 currentPos = fromDevice->pos();
    AbstractJobHandler::SupportAction action = AbstractJobHandler::SupportAction::kNoAction;

    if (Q_UNLIKELY(!stateCheck())) {
        return false;
    }
    do {
        readSize = fromDevice->read(data, blockSize);
        if (Q_UNLIKELY(!stateCheck())) {
            return false;
        }

        if (Q_UNLIKELY(readSize <= 0)) {
            if (readSize == 0 && fromInfo->size() == fromDevice->pos()) {
                return true;
            }
            fromInfo->refresh();
            AbstractJobHandler::JobErrorType errortype = fromInfo->exists() ? AbstractJobHandler::JobErrorType::kReadError : AbstractJobHandler::JobErrorType::kNonexistenceError;
            QString errorstr = fromInfo->exists() ? QString(QObject::tr("DFileCopyMoveJob", "Failed to read the file, cause: %1")).arg("to something!") : QString();

            action = doHandleErrorAndWait(fromInfo->url(), toInfo->url(), errortype, errorstr);

            if (action == AbstractJobHandler::SupportAction::kRetryAction) {
                if (!fromDevice->seek(currentPos)) {
                    AbstractJobHandler::SupportAction action = doHandleErrorAndWait(fromInfo->url(),
                                                                                    toInfo->url(),
                                                                                    AbstractJobHandler::JobErrorType::kSeekError);
                    result = action == AbstractJobHandler::SupportAction::kSkipAction;
                    skipWritSize += result ? fromInfo->size() - currentPos : 0;
                    return false;
                }
            }
        }
    } while (action == AbstractJobHandler::SupportAction::kRetryAction);

    if (action != AbstractJobHandler::SupportAction::kNoAction) {
        result = action == AbstractJobHandler::SupportAction::kSkipAction;
        return false;
    }

    return true;
}

bool DoCopyFilesWorker::doWriteFile(const AbstractFileInfoPointer &fromInfo, const AbstractFileInfoPointer &toInfo, const QSharedPointer<DFile> &toDevice, const char *data, const qint64 &readSize, bool &result)
{
    qint64 currentPos = toDevice->pos();
    AbstractJobHandler::SupportAction action = AbstractJobHandler::SupportAction::kNoAction;
    qint64 sizeWrite = 0;
    qint64 surplusSize = readSize;
    do {
        const char *surplusData = data;
        do {
            surplusData += sizeWrite;
            surplusSize -= sizeWrite;
            sizeWrite = toDevice->write(surplusData, surplusSize);
            if (Q_UNLIKELY(!stateCheck()))
                return false;
        } while (sizeWrite > 0 && sizeWrite < surplusSize);

        // 表示全部数据写入完成
        if (sizeWrite > 0) {
            break;
        }
        QString errorstr = QString(QObject::tr("Failed to write the file, cause: %1")).arg("some thing to do!");

        AbstractJobHandler::SupportAction action = AbstractJobHandler::SupportAction::kNoAction;
        action = doHandleErrorAndWait(fromInfo->url(), toInfo->url(), AbstractJobHandler::JobErrorType::kWriteError, errorstr);
        if (action == AbstractJobHandler::SupportAction::kRetryAction) {
            if (!toDevice->seek(currentPos)) {
                AbstractJobHandler::SupportAction action = doHandleErrorAndWait(fromInfo->url(),
                                                                                toInfo->url(),
                                                                                AbstractJobHandler::JobErrorType::kSeekError);
                result = action == AbstractJobHandler::SupportAction::kSkipAction;
                currentWritSize += readSize - surplusSize;
                skipWritSize += result ? fromInfo->size() - (currentPos + readSize - surplusSize) : 0;
                return false;
            }
        }
    } while (action == AbstractJobHandler::SupportAction::kRetryAction);

    if (action != AbstractJobHandler::SupportAction::kNoAction) {
        result = action == AbstractJobHandler::SupportAction::kSkipAction;
        currentWritSize += readSize - surplusSize;
        skipWritSize += result ? fromInfo->size() - (currentPos + readSize - surplusSize) : 0;
        return false;
    }

    if (sizeWrite > 0) {
        toDevice->flush();
    }
    currentWritSize += readSize;

    return true;
}

bool DoCopyFilesWorker::checkAndcopyDir(const AbstractFileInfoPointer &fromInfo,
                                        const AbstractFileInfoPointer &toInfo)
{
    emitCurrentTaskNotify(fromInfo->url(), toInfo->url());
    // 检查文件的一些合法性，源文件是否存在，创建新的目标目录名称，检查新创建目标目录名称是否存在
    AbstractJobHandler::SupportAction action = AbstractJobHandler::SupportAction::kNoAction;
    if (!toInfo->exists()) {
        do {
            if (handler.mkdir(toInfo->url())) {
                break;
            }
            action = doHandleErrorAndWait(fromInfo->url(), toInfo->url(), AbstractJobHandler::JobErrorType::kMkdirError, QString(QObject::tr("Fail to create symlink, cause: %1")).arg(handler.errorString()));
        } while (action == AbstractJobHandler::SupportAction::kRetryAction);

        if (AbstractJobHandler::SupportAction::kNoAction != action) {
            // skip write size += all file size in sources dir
            skipWritSize += dirSize;
            return AbstractJobHandler::SupportAction::kSkipAction == action;
        }

        if (jobFlags.testFlag(AbstractJobHandler::JobFlag::kCopyToSelf)) {
            return true;
        }
    }
    QFileDevice::Permissions permissions = fromInfo->permissions();
    if (fromInfo->countChildFile() <= 0) {
        handler.setPermissions(toInfo->url(), permissions);
        return true;
    }
    // 遍历源文件，执行一个一个的拷贝
    QString error;
    const AbstractDirIteratorPointer &iterator = DirIteratorFactory::create<AbstractDirIterator>(fromInfo->url(), &error);
    if (iterator) {
        doHandleErrorAndWait(fromInfo->url(), toInfo->url(), AbstractJobHandler::JobErrorType::kProrogramError, QString(QObject::tr("create dir's iterator failed, case : %1")).arg(error));
        return false;
    }

    while (iterator->hasNext()) {
        if (!stateCheck()) {
            return false;
        }

        const QUrl &url = iterator->next();
        Q_UNUSED(url);
        const AbstractFileInfoPointer &info = iterator->fileInfo();
        if (!doCopyFile(info, toInfo)) {
            return false;
        }
    }
    handler.setPermissions(toInfo->url(), permissions);
    return true;
}
/*!
 * \brief DoCopyFilesWorker::setTargetPermissions Modify the last access time of the target file and add the target file permissions
 * \param fromInfo sourc file information
 * \param toInfo target file information
 */
void DoCopyFilesWorker::setTargetPermissions(const AbstractFileInfoPointer &fromInfo, const AbstractFileInfoPointer &toInfo)
{
    // 修改文件修改时间
    handler.setFileTime(toInfo->url(), fromInfo->lastRead(), fromInfo->lastModified());
    QFileDevice::Permissions permissions = fromInfo->permissions();
    QString path = fromInfo->url().path();
    //权限为0000时，源文件已经被删除，无需修改新建的文件的权限为0000
    if (permissions != 0000)
        handler.setPermissions(toInfo->url(), permissions);
}
/*!
 * \brief DoCopyFilesWorker::verifyFileIntegrity Verify the integrity of the target file
 * \param blockSize read buffer size
 * \param sourceCheckSum source file sum
 * \param fromInfo sourc file information
 * \param toInfo target file information
 * \param toDevice target file device
 * \return the reslut
 */
bool DoCopyFilesWorker::verifyFileIntegrity(const qint64 &blockSize,
                                            const ulong &sourceCheckSum,
                                            const AbstractFileInfoPointer &fromInfo,
                                            const AbstractFileInfoPointer &toInfo,
                                            QSharedPointer<DFile> &toDevice)
{
    if (!jobFlags.testFlag(AbstractJobHandler::JobFlag::kCopyIntegrityChecking))
        return true;
    char *data = new char[blockSize + 1];
    QTime t;
    ulong targetCheckSum = adler32(0L, nullptr, 0);
    Q_FOREVER {
        qint64 size = toDevice->read(data, blockSize);

        if (Q_UNLIKELY(size <= 0)) {
            if (size == 0 && toInfo->size() == toDevice->pos()) {
                break;
            }

            QString errorstr = QObject::tr("File integrity was damaged, cause: %1").arg("some error occ!");
            AbstractJobHandler::SupportAction action = AbstractJobHandler::SupportAction::kNoAction;
            action = doHandleErrorAndWait(fromInfo->url(), toInfo->url(), AbstractJobHandler::JobErrorType::kIntegrityCheckingError, errorstr);
            if (AbstractJobHandler::SupportAction::kRetryAction == action) {
                continue;
            } else {
                return action == AbstractJobHandler::SupportAction::kSkipAction;
            }
        }

        targetCheckSum = adler32(targetCheckSum, reinterpret_cast<Bytef *>(data), static_cast<uInt>(size));

        if (Q_UNLIKELY(!stateCheck())) {
            delete[] data;
            data = nullptr;
            return false;
        }
    }
    delete[] data;

    qDebug("Time spent of integrity check of the file: %d", t.elapsed());

    if (sourceCheckSum != targetCheckSum) {
        qWarning("Failed on file integrity checking, source file: 0x%lx, target file: 0x%lx", sourceCheckSum, targetCheckSum);
        QString errorstr = QObject::tr("File integrity was damaged, cause: %1").arg("some error occ!");
        AbstractJobHandler::SupportAction action = AbstractJobHandler::SupportAction::kNoAction;
        action = doHandleErrorAndWait(fromInfo->url(), toInfo->url(), AbstractJobHandler::JobErrorType::kIntegrityCheckingError, errorstr);
        return action == AbstractJobHandler::SupportAction::kSkipAction;
    }
    return true;
}
/*!
 * \brief DoCopyFilesWorker::endCopy  Clean up the current copy task and send the copy task completion signal
 */
void DoCopyFilesWorker::endCopy()
{
    emit finishedNotify();
}
/*!
 * \brief DoCopyFilesWorker::emitErrorNotify send ErrorNotify signal
 * \param from source url
 * \param to target url
 * \param error error type
 * \param errorMsg error message
 */
void DoCopyFilesWorker::emitErrorNotify(const QUrl &from, const QUrl &to, const AbstractJobHandler::JobErrorType &error, const QString &errorMsg)
{
    JobInfoPointer info = createCopyJobInfo(from, to);
    info->insert(AbstractJobHandler::NotifyInfoKey::kErrorTypeKey, QVariant::fromValue(error));
    info->insert(AbstractJobHandler::NotifyInfoKey::kErrorMsgKey, QVariant::fromValue(errorToString(error) + errorMsg));
    info->insert(AbstractJobHandler::NotifyInfoKey::kActionsKey, QVariant::fromValue(supportActions(error)));
    emit errorNotify(info);
}
/*!
 * \brief DoCopyFilesWorker::emitProccessChangedNotify send ProccessChangedNotify signal
 * \param writSize current write data size
 */
void DoCopyFilesWorker::emitProccessChangedNotify(const qint64 &writSize)
{
    JobInfoPointer info(new QMap<quint8, QVariant>);
    info->insert(AbstractJobHandler::NotifyInfoKey::kJobtypeKey, QVariant::fromValue(AbstractJobHandler::JobType::kCopyType));
    info->insert(AbstractJobHandler::NotifyInfoKey::kTotalSizeKey, QVariant::fromValue(int(sourceFilesTotalSize)));
    info->insert(AbstractJobHandler::NotifyInfoKey::kTotalSizeKey, QVariant::fromValue(int(writSize)));

    emit proccessChangedNotify(info);
}
/*!
 * \brief DoCopyFilesWorker::emitStateChangedNotify send StateChangedNotify signal
 */
void DoCopyFilesWorker::emitStateChangedNotify()
{
    JobInfoPointer info(new QMap<quint8, QVariant>);
    info->insert(AbstractJobHandler::NotifyInfoKey::kJobtypeKey, QVariant::fromValue(AbstractJobHandler::JobType::kCopyType));
    info->insert(AbstractJobHandler::NotifyInfoKey::kJobStateKey, QVariant::fromValue(currentState));

    emit stateChangedNotify(info);
}
/*!
 * \brief DoCopyFilesWorker::emitCurrentTaskNotify send CurrentTaskNotify signal
 * \param from source url
 * \param to target url
 */
void DoCopyFilesWorker::emitCurrentTaskNotify(const QUrl &from, const QUrl &to)
{
    JobInfoPointer info = createCopyJobInfo(from, to);
    emit errorNotify(info);
}
/*!
 * \brief DoCopyFilesWorker::emitSpeedUpdatedNotify send  speedUpdatedNotify signal
 * \param writSize current write data size
 */
void DoCopyFilesWorker::emitSpeedUpdatedNotify(const qint64 &writSize)
{
    JobInfoPointer info(new QMap<quint8, QVariant>);
    qint64 speed = writSize * 1000 / time.elapsed();
    info->insert(AbstractJobHandler::NotifyInfoKey::kSpeedKey, QVariant::fromValue(speed));
    info->insert(AbstractJobHandler::NotifyInfoKey::kRemindTimeKey, QVariant::fromValue((sourceFilesTotalSize - writSize) / speed));

    emit stateChangedNotify(info);
}
/*!
 * \brief DoCopyFilesWorker::stateCheck Blocking waiting for copy and check status
 * \return is Correct state
 */
bool DoCopyFilesWorker::stateCheck()
{
    if (currentState == AbstractJobHandler::JobState::kRunningState) {
        return true;
    }
    if (currentState == AbstractJobHandler::JobState::kPauseState) {
        qInfo() << "Will be suspended";
        if (!copyWait()) {
            return currentState != AbstractJobHandler::JobState::kStopState;
        }
    } else if (currentState == AbstractJobHandler::JobState::kStopState) {
        return false;
    }

    return true;
}
/*!
 * \brief DoCopyFilesWorker::copyWait Blocking waiting for copy
 * \return Is it running
 */
bool DoCopyFilesWorker::copyWait()
{
    conditionMutex.lock();
    waitCondition.wait(&conditionMutex);
    conditionMutex.unlock();

    return currentState == AbstractJobHandler::JobState::kRunningState;
}
/*!
 * \brief DoCopyFilesWorker::doHandleErrorAndWait Blocking handles errors and returns
 * actions supported by the operation
 * \param from source information
 * \param to target information
 * \param error error type
 * \param errorMsg error message
 * \return support action
 */
AbstractJobHandler::SupportAction DoCopyFilesWorker::doHandleErrorAndWait(const QUrl &from, const QUrl &to,
                                                                          const AbstractJobHandler::JobErrorType &error,
                                                                          const QString &errorMsg)
{
    setStat(AbstractJobHandler::JobState::kPauseState);
    errorConditioMutex.lock();
    if (!handlingErrorCondition)
        // 处理自己的错误
        handlingErrorCondition.reset(new QWaitCondition);

    if (errorConditionQueue.count() <= 0) {
        emitErrorNotify(from, to, error, errorMsg);
        errorConditionQueue.enqueue(handlingErrorCondition);
    }
    errorConditioMutex.unlock();
    QMutex lock;
    handlingErrorCondition->wait(&lock);
    lock.unlock();
    return currentAction;
}
/*!
 * \brief DoCopyFilesWorker::setStat Set current task status
 * \param stat task status
 */
void DoCopyFilesWorker::setStat(const AbstractJobHandler::JobState &stat)
{
    if (stat == currentState)
        return;
    currentState = stat;
    emitStateChangedNotify();
}
/*!
 * \brief DoCopyFilesWorker::supportActions get the support Acttions by error type
 * \param error error type
 * \return support action
 */
AbstractJobHandler::SupportActions DoCopyFilesWorker::supportActions(const AbstractJobHandler::JobErrorType &error)
{
    AbstractJobHandler::SupportActions support = AbstractJobHandler::SupportAction::kCancelAction;
    switch (error) {
    case AbstractJobHandler::JobErrorType::kPermissionError:
    case AbstractJobHandler::JobErrorType::kOpenError:
    case AbstractJobHandler::JobErrorType::kReadError:
    case AbstractJobHandler::JobErrorType::kWriteError:
    case AbstractJobHandler::JobErrorType::kSymlinkError:
    case AbstractJobHandler::JobErrorType::kMkdirError:
    case AbstractJobHandler::JobErrorType::kResizeError:
    case AbstractJobHandler::JobErrorType::kRemoveError:
    case AbstractJobHandler::JobErrorType::kRenameError:
    case AbstractJobHandler::JobErrorType::kIntegrityCheckingError:
        return support | AbstractJobHandler::SupportAction::kSkipAction | AbstractJobHandler::SupportAction::kRetryAction;
    case AbstractJobHandler::JobErrorType::kSpecialFileError:
        return AbstractJobHandler::SupportAction::kSkipAction;
    case AbstractJobHandler::JobErrorType::kFileSizeTooBigError:
        return support | AbstractJobHandler::SupportAction::kSkipAction | AbstractJobHandler::SupportAction::kEnforceAction;
    case AbstractJobHandler::JobErrorType::kNotEnoughSpaceError:
        return support | AbstractJobHandler::SupportAction::kSkipAction | AbstractJobHandler::SupportAction::kRetryAction | AbstractJobHandler::SupportAction::kEnforceAction;
    case AbstractJobHandler::JobErrorType::kFileExistsError:
        return support | AbstractJobHandler::SupportAction::kSkipAction | AbstractJobHandler::SupportAction::kReplaceAction | AbstractJobHandler::SupportAction::kCoexistAction;
    case AbstractJobHandler::JobErrorType::kDirectoryExistsError:
        return support | AbstractJobHandler::SupportAction::kSkipAction | AbstractJobHandler::SupportAction::kMergeAction | AbstractJobHandler::SupportAction::kCoexistAction;
    case AbstractJobHandler::JobErrorType::kTargetReadOnlyError:
        return support | AbstractJobHandler::SupportAction::kSkipAction | AbstractJobHandler::SupportAction::kEnforceAction;
    case AbstractJobHandler::JobErrorType::kTargetIsSelfError:
        return support | AbstractJobHandler::SupportAction::kSkipAction | AbstractJobHandler::SupportAction::kEnforceAction;
    case AbstractJobHandler::JobErrorType::kSymlinkToGvfsError:
        return support | AbstractJobHandler::SupportAction::kSkipAction;
    default:
        break;
    }

    return support;
}
/*!
 * \brief DoCopyFilesWorker::errorToString get error msg by error type
 * \param error error type
 * \return error message
 */
QString DoCopyFilesWorker::errorToString(const AbstractJobHandler::JobErrorType &error)
{
    switch (error) {
    case AbstractJobHandler::JobErrorType::kPermissionError:
        return QObject::tr("Permission error");
    case AbstractJobHandler::JobErrorType::kSpecialFileError:
        return QObject::tr("The action is denied");
    case AbstractJobHandler::JobErrorType::kFileExistsError:
        return QObject::tr("Target file is exists");
    case AbstractJobHandler::JobErrorType::kDirectoryExistsError:
        return QObject::tr("Target directory is exists");
    case AbstractJobHandler::JobErrorType::kOpenError:
        return QObject::tr("Failed to open the file");
    case AbstractJobHandler::JobErrorType::kReadError:
        return QObject::tr("Failed to read the file");
    case AbstractJobHandler::JobErrorType::kWriteError:
        return QObject::tr("Failed to write the file");
    case AbstractJobHandler::JobErrorType::kMkdirError:
        return QObject::tr("Failed to create the directory");
    case AbstractJobHandler::JobErrorType::kRemoveError:
        return QObject::tr("Failed to delete the file");
    case AbstractJobHandler::JobErrorType::kRenameError:
        return QObject::tr("Failed to move the file");
    case AbstractJobHandler::JobErrorType::kNonexistenceError:
        return QObject::tr("Original file does not exist");
    case AbstractJobHandler::JobErrorType::kFileSizeTooBigError:
        return QObject::tr("Failed, file size must be less than 4GB");
    case AbstractJobHandler::JobErrorType::kNotEnoughSpaceError:
        return QObject::tr("Not enough free space on the target disk");
    case AbstractJobHandler::JobErrorType::kIntegrityCheckingError:
        return QObject::tr("File integrity was damaged");
    case AbstractJobHandler::JobErrorType::kTargetReadOnlyError:
        return QObject::tr("The target device is read only");
    case AbstractJobHandler::JobErrorType::kTargetIsSelfError:
        return QObject::tr("Target folder is inside the source folder");
    case AbstractJobHandler::JobErrorType::kNotSupportedError:
        return QObject::tr("The action is not supported");
    case AbstractJobHandler::JobErrorType::kPermissionDeniedError:
        return QObject::tr("You do not have permission to traverse files in it");
    default:
        break;
    }

    return QString();
}
/*!
 * \brief DoCopyFilesWorker::createCopyJobInfo create emit signal job infor pointer
 * \param from source url
 * \param to target url
 * \return Pointer to signal information
 */
JobInfoPointer DoCopyFilesWorker::createCopyJobInfo(const QUrl &from, const QUrl &to)
{
    JobInfoPointer info(new QMap<quint8, QVariant>);
    info->insert(AbstractJobHandler::NotifyInfoKey::kJobtypeKey, QVariant::fromValue(AbstractJobHandler::JobType::kCopyType));
    info->insert(AbstractJobHandler::NotifyInfoKey::kSourceUrlKey, QVariant::fromValue(from));
    info->insert(AbstractJobHandler::NotifyInfoKey::kSourceUrlKey, QVariant::fromValue(to));
    QString fromMsg = QString(QObject::tr("copy file %1")).arg(from.path());
    info->insert(AbstractJobHandler::NotifyInfoKey::kSourceMsgKey, QVariant::fromValue(fromMsg));
    QString toMsg = QString(QObject::tr("to %1")).arg(to.path());
    info->insert(AbstractJobHandler::NotifyInfoKey::kTargetMsgKey, QVariant::fromValue(toMsg));
    return info;
}
/*!
 * \brief DoCopyFilesWorker::formatFileName Processing and formatting file names
 * \param fileName file name
 * \return format file name
 */
QString DoCopyFilesWorker::formatFileName(const QString &fileName)
{
    // 获取目标文件的文件系统，是vfat格式是否要特殊处理，以前的文管处理的
    if (jobFlags.testFlag(AbstractJobHandler::JobFlag::kDontFormatFileName)) {
        return fileName;
    }
    if (!targetStorageInfo) {
        targetStorageInfo.reset(new StorageInfo(target.path()));
    }
    const QString &fs_type = targetStorageInfo->fileSystemType();

    if (fs_type == "vfat") {
        QString new_name = fileName;

        return new_name.replace(QRegExp("[\"*:<>?\\|]"), "_");
    }

    return fileName;
}
/*!
 * \brief DoCopyFilesWorker::getNonExistFileName Gets the name of a file that does not exist
 * \param fromInfo Source file information
 * \param targetDir Target directory information
 * \return file name
 */
QString DoCopyFilesWorker::getNonExistFileName(const AbstractFileInfoPointer fromInfo, const AbstractFileInfoPointer targetDir)
{
    if (!targetDir || !targetDir->exists()) {
        // TODO:: paused and handle error
        return QString();
    }
    const QString &copy_text = QString(QObject::tr("Extra name added to new file name when used for file name."));

    AbstractFileInfoPointer targetFileInfo { nullptr };
    QString fileBaseName = fromInfo->baseName();
    QString suffix = fromInfo->suffix();
    QString fileName = fromInfo->fileName();
    //在7z分卷压缩后的名称特殊处理7z.003
    if (fileName.contains(QRegularExpression(".7z.[0-9]{3,10}$"))) {
        fileBaseName = fileName.left(fileName.indexOf(QRegularExpression(".7z.[0-9]{3,10}$")));
        suffix = fileName.mid(fileName.indexOf(QRegularExpression(".7z.[0-9]{3,10}$")) + 1);
    }

    int number = 0;

    QString newFileName;

    do {
        newFileName = number > 0 ? QString("%1(%2 %3)").arg(fileBaseName, copy_text).arg(number) : QString("%1(%2)").arg(fileBaseName, copy_text);

        if (!suffix.isEmpty()) {
            newFileName.append('.').append(suffix);
        }

        ++number;
        QUrl newUrl;
        newUrl = targetDir->url();
        newUrl.setPath(newUrl.path() + "/" + newFileName);
        targetFileInfo = InfoFactory::create<AbstractFileInfo>(newUrl);
    } while (targetFileInfo && targetFileInfo->exists());

    return newFileName;
}
/*!
 * \brief DoCopyFilesWorker::getWriteDataSize Gets the size of the data being written
 * \return write data size
 */
qint64 DoCopyFilesWorker::getWriteDataSize()
{
    if (CountWriteSizeType::kTidType == countWriteType) {
        return getTidWriteSize();
    } else if (CountWriteSizeType::kCustomizeType == countWriteType) {
        return currentWritSize;
    }

    if (targetDeviceStartSectorsWritten >= 0) {
        if ((getSectorsWritten() == 0) && (targetDeviceStartSectorsWritten > 0)) {
            return 0;
        } else {
            return (getSectorsWritten() - targetDeviceStartSectorsWritten) * targetLogSecionSize;
        }
    }

    return currentWritSize;
}
/*!
 * \brief DoCopyFilesWorker::getTidWriteSize Get the write data size by using the thread ID
 * \return write data size
 */
qint64 DoCopyFilesWorker::getTidWriteSize()
{
    QFile file(QStringLiteral("/proc/self/task/%1/io").arg(copyTid));

    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed on open the" << file.fileName() << ", will be not update the job speed and progress";

        return 0;
    }

    const QByteArray &line_head = QByteArrayLiteral("write_bytes: ");
    const QByteArray &all_data = file.readAll();

    file.close();

    QTextStream text_stream(all_data);

    while (!text_stream.atEnd()) {
        const QByteArray &line = text_stream.readLine().toLatin1();

        if (line.startsWith(line_head)) {
            bool ok = false;
            qint64 size = line.mid(line_head.size()).toLongLong(&ok);

            if (!ok) {
                qWarning() << "Failed to convert to qint64, line string=" << line;

                return 0;
            }
            return size;
        }
    }

    qWarning() << "Failed to find \"" << line_head << "\" from the" << file.fileName();

    return 0;
}
/*!
 * \brief DoCopyFilesWorker::getSectorsWritten Get write Sector or writ block
 * \return write Sector or writ block
 */
qint64 DoCopyFilesWorker::getSectorsWritten()
{
    QByteArray data;
    QFile file(targetSysDevPath + "/stat");

    if (file.open(QIODevice::ReadOnly)) {
        data = file.readAll();
        file.close();
        return data.simplified().split(' ').value(6).toLongLong();
    } else {
        return 0;
    }
}
/*!
 * \brief DoCopyFilesWorker::readAheadSourceFile Read ahead file
 * \param fileInfo file information
 */
void DoCopyFilesWorker::readAheadSourceFile(const AbstractFileInfoPointer &fileInfo)
{
    if (fileInfo->size() <= 0)
        return;
    std::string stdStr = fileInfo->url().path().toUtf8().toStdString();
    int fromfd = open(stdStr.data(), O_RDONLY);
    if (-1 != fromfd) {
        readahead(fromfd, 0, static_cast<size_t>(fileInfo->size()));
        close(fromfd);
    }
}
/*!
 * \brief DoCopyFilesWorker::onStatisticsFilesSizeFinish  Count the size of all files
 * and the slot at the end of the thread
 * \param sizeInfo All file size information
 */
void DoCopyFilesWorker::onStatisticsFilesSizeFinish(const SizeInfoPoiter sizeInfo)
{
    sourceFilesTotalSize = sizeInfo->totalSize;
    dirSize = sizeInfo->dirSize;
}
/*!
 * \brief DoCopyFilesWorker::onUpdateProccess update proccess and speed slot
 */
void DoCopyFilesWorker::onUpdateProccess()
{
    // 当前写入文件大小获取
    qint64 writSize = getWriteDataSize();
    writSize += skipWritSize;
    emitProccessChangedNotify(writSize);
    emitSpeedUpdatedNotify(writSize);
}
