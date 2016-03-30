#ifndef DBOOKMARKROOTITEM_H
#define DBOOKMARKROOTITEM_H

#include "dbookmarkitem.h"

class DBookmarkScene;

class DBookmarkRootItem : public DBookmarkItem
{
public:
    DBookmarkRootItem(DBookmarkScene * scene);
protected:
    void dragEnterEvent(QGraphicsSceneDragDropEvent *event);
    void dropEvent(QGraphicsSceneDragDropEvent *event);
private:
    DBookmarkScene * m_scene;
};

#endif // DBOOKMARKROOTITEM_H
