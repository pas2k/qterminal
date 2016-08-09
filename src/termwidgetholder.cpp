/***************************************************************************
 *   Copyright (C) 2010 by Petr Vanek                                      *
 *   petr@scribus.info                                                     *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>. *
 ***************************************************************************/

#include <QGridLayout>
#include <QSplitter>
#include <QInputDialog>

#include "qterminalapp.h"
#include "mainwindow.h"
#include "termwidgetholder.h"
#include "termwidget.h"
#include "properties.h"
#include <assert.h>
#include <climits>
#include <algorithm>


TermWidgetHolder::TermWidgetHolder(TerminalConfig &config, QWidget * parent)
    : QWidget(parent),
      m_currentTerm(0)
{
    setFocusPolicy(Qt::NoFocus);
    QGridLayout * lay = new QGridLayout(this);
    lay->setSpacing(0);
    lay->setContentsMargins(0, 0, 0, 0);

    QSplitter *s = new QSplitter(this);
    s->setFocusPolicy(Qt::NoFocus);
    TermWidget *w = newTerm(config);
    s->addWidget(w);
    lay->addWidget(s);

    setLayout(lay);
}

TermWidgetHolder::~TermWidgetHolder()
{
}

void TermWidgetHolder::setInitialFocus()
{
    QList<TermWidget*> list = findChildren<TermWidget*>();
    TermWidget * w = list.count() == 0 ? 0 : list.at(0);
    if (w)
        w->setFocus(Qt::OtherFocusReason);
}

void TermWidgetHolder::loadSession()
{
    bool ok;
    QString name = QInputDialog::getItem(this, tr("Load Session"),
                                         tr("List of saved sessions:"),
                                         Properties::Instance()->sessions.keys(),
                                         0, false, &ok);
    if (!ok || name.isEmpty())
        return;
#if 0
    foreach (QWidget * w, findChildren<QWidget*>())
    {
        if (w)
        {
            delete w;
            w = 0;
        }
    }

    qDebug() << "load" << name << QString(Properties::Instance()->sessions[name]);
    QStringList splitters = QString(Properties::Instance()->sessions[name]).split("|", QString::SkipEmptyParts);
    foreach (QString splitter, splitters)
    {
        QStringList components = splitter.split(",");
        qDebug() << "comp" << components;
        // orientation
        Qt::Orientation orientation;
        if (components.size() > 0)
            orientation = components.takeAt(0).toInt();
        // sizes
        QList<int> sizes;
        QList<TermWidget*> widgets;
        foreach (QString s, components)
        {
            sizes << s.toInt();
            widgets << newTerm();
        }
        // new terms
    }
#endif
}

void TermWidgetHolder::saveSession(const QString & name)
{
    Session dump;
    QString num("%1");
    foreach(QSplitter *w, findChildren<QSplitter*>())
    {
        dump += '|' + num.arg(w->orientation());
        foreach (int i, w->sizes())
            dump += ',' + num.arg(i);
    }
    Properties::Instance()->sessions[name] = dump;
    qDebug() << "dump" << dump;
}

TermWidget* TermWidgetHolder::currentTerminal()
{
    return m_currentTerm;
}

void TermWidgetHolder::setWDir(const QString & wdir)
{
    m_wdir = wdir;
}

typedef struct  {
    QPoint topLeft;
    QPoint middle;
    QPoint bottomRight;
} NavigationData;

static void transpose(QPoint *point) {
    int x = point->x();
    point->setX(point->y());
    point->setY(x);
}

static void transposeTransform(NavigationData *point) {
    transpose(&point->topLeft);
    transpose(&point->middle);
    transpose(&point->bottomRight);
}

static void flipTransform(NavigationData *point) {
    QPoint oldTopLeft = point->topLeft;
    point->topLeft = -(point->bottomRight);
    point->bottomRight = -(oldTopLeft);
    point->middle = -(point->middle);
}

static void normalizeToRight(NavigationData *point, NavigationDirection dir) {
    switch (dir) {
        case Left:
            flipTransform(point);
            break;
        case Right:
            // No-op
            break;
        case Top:
            flipTransform(point);
            transposeTransform(point);
            break;
        case Bottom:
            transposeTransform(point);
            break;
        default:
            assert("Invalid navigation");
            return;
    }
}

static NavigationData getNormalizedDimensions(QWidget *w, NavigationDirection dir) {
    NavigationData nd;
    nd.topLeft = w->mapTo(w->window(), QPoint(0, 0));
    nd.middle = w->mapTo(w->window(), QPoint(w->width() / 2, w->height() / 2));
    nd.bottomRight = w->mapTo(w->window(), QPoint(w->width(), w->height()));
    normalizeToRight(&nd, dir);
    return nd;
}


void TermWidgetHolder::directionalNavigation(NavigationDirection dir) {
    // Find an active widget
    QList<TermWidget*> l = findChildren<TermWidget*>();
    int ix = -1;
    foreach (TermWidget * w, l)
    {
        ++ix;
        if (w->impl()->hasFocus())
        {
            break;
        }
    }
    if (ix > l.count())
    {
        l.at(0)->impl()->setFocus(Qt::OtherFocusReason);
        return;
    }

    // Found an active widget
    TermWidget *w = l.at(ix);
    NavigationData from = getNormalizedDimensions(w, dir);

    // Search parent that contains point of interest (right edge middlepoint)
    QPoint poi = QPoint(from.bottomRight.x(), from.middle.y());

    // Perform a search for a TermWidget, where x() is strictly higher than
    // poi.x(), y() is strictly less than poi.y(), and prioritizing, in order,
    // lower x(), and lower distance between poi.y() and corners.

    // Only "Right navigation" implementation is necessary -- other cases
    // are normalized to this one.

    l = findChildren<TermWidget*>();
    int lowestX = INT_MAX;
    int lowestMidpointDistance = INT_MAX;
    TermWidget *fittest = NULL;
    foreach (TermWidget * w, l) 
    {
        NavigationData contenderDims = getNormalizedDimensions(w, dir);
        int midpointDistance = std::min(
            abs(poi.y() - contenderDims.topLeft.y()),
            abs(poi.y() - contenderDims.bottomRight.y())
        );
        if (contenderDims.topLeft.x() > poi.x()) 
        {
            if (contenderDims.topLeft.x() > lowestX)
                continue;
            if (midpointDistance > lowestMidpointDistance)
                continue;
            lowestX = contenderDims.topLeft.x();
            lowestMidpointDistance = midpointDistance;
            fittest = w;
        }
    }
    if (fittest != NULL) {
        fittest->impl()->setFocus(Qt::OtherFocusReason);
    }
}

void TermWidgetHolder::clearActiveTerminal()
{
    currentTerminal()->impl()->clear();
}

void TermWidgetHolder::propertiesChanged()
{
    foreach(TermWidget *w, findChildren<TermWidget*>())
        w->propertiesChanged();
}

void TermWidgetHolder::splitHorizontal(TermWidget * term)
{
    TerminalConfig defaultConfig;
    split(term, Qt::Vertical, defaultConfig);
}

void TermWidgetHolder::splitVertical(TermWidget * term)
{
    TerminalConfig defaultConfig;
    split(term, Qt::Horizontal, defaultConfig);
}

void TermWidgetHolder::splitCollapse(TermWidget * term)
{
    QSplitter * parent = qobject_cast<QSplitter*>(term->parent());
    assert(parent);
    term->setParent(0);
    delete term;

    QWidget *nextFocus = Q_NULLPTR;

    // Collapse splitters containing a single element, excluding the top one.
    if (parent->count() == 1)
    {
        QSplitter *uselessSplitterParent = qobject_cast<QSplitter*>(parent->parent());
        if (uselessSplitterParent != Q_NULLPTR) {
            int idx = uselessSplitterParent->indexOf(parent);
            assert(idx != -1);
            QWidget *singleHeir = parent->widget(0);
            uselessSplitterParent->insertWidget(idx, singleHeir);
            if (qobject_cast<TermWidget*>(singleHeir))
            {
                nextFocus = singleHeir;
            }
            else
            {
                nextFocus = singleHeir->findChild<TermWidget*>();
            }
            parent->setParent(0);
            delete parent;
            // Make sure there's no access to the removed parent
            parent = uselessSplitterParent;
        }
    }

    if (parent->count() > 0)
    {
        if (nextFocus)
        {
            nextFocus->setFocus(Qt::OtherFocusReason);
        }
        else
        {
            parent->widget(0)->setFocus(Qt::OtherFocusReason);
        }
        parent->update();
    }
    else
        emit finished();
}

TermWidget * TermWidgetHolder::split(TermWidget *term, Qt::Orientation orientation, TerminalConfig cfg)
{
    QSplitter *parent = qobject_cast<QSplitter *>(term->parent());
    assert(parent);

    int ix = parent->indexOf(term);
    QList<int> parentSizes = parent->sizes();

    QList<int> sizes;
    sizes << 1 << 1;

    QSplitter *s = new QSplitter(orientation, this);
    s->setFocusPolicy(Qt::NoFocus);
    s->insertWidget(0, term);

    cfg.provideCurrentDirectory(term->impl()->workingDirectory());
    
    TermWidget * w = newTerm(cfg);
    s->insertWidget(1, w);
    s->setSizes(sizes);

    parent->insertWidget(ix, s);
    parent->setSizes(parentSizes);

    w->setFocus(Qt::OtherFocusReason);
    return w;
}

TermWidget *TermWidgetHolder::newTerm(TerminalConfig &cfg)
{
    TermWidget *w = new TermWidget(cfg, this);
    // proxy signals
    connect(w, SIGNAL(renameSession()), this, SIGNAL(renameSession()));
    connect(w, SIGNAL(removeCurrentSession()), this, SIGNAL(lastTerminalClosed()));
    connect(w, SIGNAL(finished()), this, SLOT(handle_finished()));
    // consume signals

    connect(w, SIGNAL(splitHorizontal(TermWidget *)),
            this, SLOT(splitHorizontal(TermWidget *)));
    connect(w, SIGNAL(splitVertical(TermWidget *)),
            this, SLOT(splitVertical(TermWidget *)));
    connect(w, SIGNAL(splitCollapse(TermWidget *)),
            this, SLOT(splitCollapse(TermWidget *)));
    connect(w, SIGNAL(termGetFocus(TermWidget *)),
            this, SLOT(setCurrentTerminal(TermWidget *)));
    connect(w, &TermWidget::termTitleChanged, this, &TermWidgetHolder::onTermTitleChanged);

    return w;
}

void TermWidgetHolder::setCurrentTerminal(TermWidget* term)
{
    TermWidget * old_current = m_currentTerm;
    m_currentTerm = term;
    if (old_current != m_currentTerm)
    {
        if (m_currentTerm->impl()->isTitleChanged())
        {
            emit termTitleChanged(m_currentTerm->impl()->title(), m_currentTerm->impl()->icon());
        } else
        {
            emit termTitleChanged(windowTitle(), QString{});
        }
    }
}

void TermWidgetHolder::handle_finished()
{
    TermWidget * w = qobject_cast<TermWidget*>(sender());
    if (!w)
    {
        qDebug() << "TermWidgetHolder::handle_finished: Unknown object to handle" << w;
        assert(0);
    }
    splitCollapse(w);
}

void TermWidgetHolder::onTermTitleChanged(QString title, QString icon) const
{
    TermWidget * term = qobject_cast<TermWidget *>(sender());
    if (m_currentTerm == term)
        emit termTitleChanged(title, icon);
}

