/*
 * Copyright (C) 2022 Uniontech Software Technology Co., Ltd.
 *
 * Author:     zhangyu<zhangyub@uniontech.com>
 *
 * Maintainer: zhangyu<zhangyub@uniontech.com>
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
#ifndef CANVASINTERFACE_H
#define CANVASINTERFACE_H

#include "ddplugin_organizer_global.h"

#include <QObject>

namespace ddplugin_organizer {

class CanvasInterfacePrivate;
class CanvasInterface : public QObject
{
    Q_OBJECT
    friend class CanvasInterfacePrivate;
public:
    explicit CanvasInterface(QObject *parent = nullptr);
    ~CanvasInterface();
    bool initialize();
    int iconLevel();
    void setIconLevel(int);
    class FileInfoModelShell *fileInfoModel();
    class CanvasModelShell *canvasModel();
    class CanvasViewShell *canvasView();
    class CanvasGridShell *canvasGrid();
signals:

public slots:
protected:

private:
    CanvasInterfacePrivate *d;
};

}

#endif // CANVASINTERFACE_H
