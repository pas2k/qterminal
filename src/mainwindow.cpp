/***************************************************************************
 *   Copyright (C) 2006 by Vladimir Kuznetsov                              *
 *   vovanec@gmail.com                                                     *
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

#include <QDockWidget>
#include <QDesktopWidget>
#include <QToolButton>
#include <QMessageBox>
#include <functional>

#include "mainwindow.h"
#include "tabwidget.h"
#include "termwidgetholder.h"
#include "config.h"
#include "properties.h"
#include "propertiesdialog.h"
#include "bookmarkswidget.h"
#include "qterminalapp.h"


typedef std::function<bool(MainWindow&)> checkfn;
Q_DECLARE_METATYPE(checkfn)

// TODO/FXIME: probably remove. QSS makes it unusable on mac...
#define QSS_DROP    "MainWindow {border: 1px solid rgba(0, 0, 0, 50%);}\n"

MainWindow::MainWindow(const QString& work_dir,
                       const QString& command,
                       bool dropMode,
                       QWidget * parent,
                       Qt::WindowFlags f)
    : QMainWindow(parent,f),
      m_initShell(command),
      m_initWorkDir(work_dir),
      m_dropLockButton(0),
      m_dropMode(dropMode)
{
    QTerminalApp::Instance()->addWindow(this);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);

    setupUi(this);
    Properties::Instance()->migrate_settings();
    Properties::Instance()->loadSettings();

    m_bookmarksDock = new QDockWidget(tr("Bookmarks"), this);
    m_bookmarksDock->setObjectName("BookmarksDockWidget");
    m_bookmarksDock->setAutoFillBackground(true);
    BookmarksWidget *bookmarksWidget = new BookmarksWidget(m_bookmarksDock);
    bookmarksWidget->setAutoFillBackground(true);
    m_bookmarksDock->setWidget(bookmarksWidget);
    addDockWidget(Qt::LeftDockWidgetArea, m_bookmarksDock);
    connect(bookmarksWidget, SIGNAL(callCommand(QString)),
            this, SLOT(bookmarksWidget_callCommand(QString)));

    connect(m_bookmarksDock, SIGNAL(visibilityChanged(bool)),
            this, SLOT(bookmarksDock_visibilityChanged(bool)));

    connect(actAbout, SIGNAL(triggered()), SLOT(actAbout_triggered()));
    connect(actAboutQt, SIGNAL(triggered()), qApp, SLOT(aboutQt()));
    connect(&m_dropShortcut, SIGNAL(activated()), SLOT(showHide()));

    setContentsMargins(0, 0, 0, 0);
    if (m_dropMode) {
        this->enableDropMode();
        setStyleSheet(QSS_DROP);
    }
    else {
	if (Properties::Instance()->saveSizeOnExit) {
	    resize(Properties::Instance()->mainWindowSize);
	}
	if (Properties::Instance()->savePosOnExit) {
	    move(Properties::Instance()->mainWindowPosition);
	}
        restoreState(Properties::Instance()->mainWindowState);
    }

    consoleTabulator->setAutoFillBackground(true);
    connect(consoleTabulator, SIGNAL(closeTabNotification()), SLOT(close()));
    consoleTabulator->setWorkDirectory(work_dir);
    consoleTabulator->setTabPosition((QTabWidget::TabPosition)Properties::Instance()->tabsPos);
    //consoleTabulator->setShellProgram(command);

    // apply props
    propertiesChanged();
    
    setupCustomDirs();

    connect(consoleTabulator, &TabWidget::currentTitleChanged, this, &MainWindow::onCurrentTitleChanged);
    connect(menu_Actions, SIGNAL(aboutToShow()), this, SLOT(aboutToShowActionsMenu()));

    /* The tab should be added after all changes are made to
       the main window; otherwise, the initial prompt might
       get jumbled because of changes in internal geometry. */
    consoleTabulator->addNewTab(command);
}

void MainWindow::rebuildActions()
{
    QMap< QString, QAction * > oldActions(actions);

    setup_FileMenu_Actions();
    setup_ActionsMenu_Actions();
    setup_ViewMenu_Actions();

    foreach (QAction *a, oldActions.values())
    {
        delete a;
    }
}

MainWindow::~MainWindow()
{
    QTerminalApp::Instance()->removeWindow(this);
}

void MainWindow::enableDropMode()
{
    setWindowFlags(Qt::Dialog | Qt::WindowStaysOnTopHint | Qt::CustomizeWindowHint);

    m_dropLockButton = new QToolButton(this);
    consoleTabulator->setCornerWidget(m_dropLockButton, Qt::BottomRightCorner);
    m_dropLockButton->setCheckable(true);
    m_dropLockButton->connect(m_dropLockButton, SIGNAL(clicked(bool)), this, SLOT(setKeepOpen(bool)));
    setKeepOpen(Properties::Instance()->dropKeepOpen);
    m_dropLockButton->setAutoRaise(true);

    setDropShortcut(Properties::Instance()->dropShortCut);
    realign();
}

void MainWindow::setDropShortcut(QKeySequence dropShortCut)
{
    if (!m_dropMode)
        return;

    if (m_dropShortcut.shortcut() != dropShortCut)
    {
        m_dropShortcut.setShortcut(dropShortCut);
        qWarning() << tr("Press \"%1\" to see the terminal.").arg(dropShortCut.toString());
    }
}

void MainWindow::setup_ActionsMenu_Actions()
{
    QSettings settings;
    settings.beginGroup("Shortcuts");

    QKeySequence seq;
    QVariant data;

    const checkfn checkTabs = &MainWindow::hasMultipleTabs;
    const checkfn checkSubterminals = &MainWindow::hasMultipleSubterminals;

    actions[CLEAR_TERMINAL] = new QAction(QIcon::fromTheme("edit-clear"), tr("&Clear Current Tab"), this);
    seq = QKeySequence::fromString(settings.value(CLEAR_TERMINAL, CLEAR_TERMINAL_SHORTCUT).toString());
    actions[CLEAR_TERMINAL]->setShortcut(seq);
    connect(actions[CLEAR_TERMINAL], SIGNAL(triggered()), consoleTabulator, SLOT(clearActiveTerminal()));
    menu_Actions->addAction(actions[CLEAR_TERMINAL]);
    addAction(actions[CLEAR_TERMINAL]);

    menu_Actions->addSeparator();

    actions[TAB_NEXT] = new QAction(QIcon::fromTheme("go-next"), tr("&Next Tab"), this);
    seq = QKeySequence::fromString( settings.value(TAB_NEXT, TAB_NEXT_SHORTCUT).toString() );
    actions[TAB_NEXT]->setShortcut(seq);
    connect(actions[TAB_NEXT], SIGNAL(triggered()), consoleTabulator, SLOT(switchToRight()));
    actions[TAB_NEXT]->setData(data);
    menu_Actions->addAction(actions[TAB_NEXT]);
    addAction(actions[TAB_NEXT]);

    actions[TAB_PREV] = new QAction(QIcon::fromTheme("go-previous"), tr("&Previous Tab"), this);
    seq = QKeySequence::fromString( settings.value(TAB_PREV, TAB_PREV_SHORTCUT).toString() );
    actions[TAB_PREV]->setShortcut(seq);
    connect(actions[TAB_PREV], SIGNAL(triggered()), consoleTabulator, SLOT(switchToLeft()));
    actions[TAB_PREV]->setData(data);
    menu_Actions->addAction(actions[TAB_PREV]);
    addAction(actions[TAB_PREV]);

    actions[MOVE_LEFT] = new QAction(tr("Move Tab &Left"), this);
    seq = QKeySequence::fromString( settings.value(MOVE_LEFT, MOVE_LEFT_SHORTCUT).toString() );
    actions[MOVE_LEFT]->setShortcut(seq);
    actions[MOVE_LEFT]->setData(data);
    connect(actions[MOVE_LEFT], SIGNAL(triggered()), consoleTabulator, SLOT(moveLeft()));
    menu_Actions->addAction(actions[MOVE_LEFT]);
    addAction(actions[MOVE_LEFT]);

    actions[MOVE_RIGHT] = new QAction(tr("Move Tab &Right"), this);
    seq = QKeySequence::fromString( settings.value(MOVE_RIGHT, MOVE_RIGHT_SHORTCUT).toString() );
    actions[MOVE_RIGHT]->setShortcut(seq);
    actions[MOVE_RIGHT]->setData(data);
    connect(actions[MOVE_RIGHT], SIGNAL(triggered()), consoleTabulator, SLOT(moveRight()));
    menu_Actions->addAction(actions[MOVE_RIGHT]);
    addAction(actions[MOVE_RIGHT]);

    menu_Actions->addSeparator();

    actions[SPLIT_HORIZONTAL] = new QAction(tr("Split Terminal &Horizontally"), this);
    seq = QKeySequence::fromString( settings.value(SPLIT_HORIZONTAL).toString() );
    actions[SPLIT_HORIZONTAL]->setShortcut(seq);
    connect(actions[SPLIT_HORIZONTAL], SIGNAL(triggered()), consoleTabulator, SLOT(splitHorizontally()));
    menu_Actions->addAction(actions[SPLIT_HORIZONTAL]);
    addAction(actions[SPLIT_HORIZONTAL]);

    actions[SPLIT_VERTICAL] = new QAction(tr("Split Terminal &Vertically"), this);
    seq = QKeySequence::fromString( settings.value(SPLIT_VERTICAL).toString() );
    actions[SPLIT_VERTICAL]->setShortcut(seq);
    connect(actions[SPLIT_VERTICAL], SIGNAL(triggered()), consoleTabulator, SLOT(splitVertically()));
    menu_Actions->addAction(actions[SPLIT_VERTICAL]);
    addAction(actions[SPLIT_VERTICAL]);

    data.setValue(checkSubterminals);

    actions[SUB_COLLAPSE] = new QAction(tr("&Collapse Subterminal"), this);
    seq = QKeySequence::fromString( settings.value(SUB_COLLAPSE).toString() );
    actions[SUB_COLLAPSE]->setShortcut(seq);
    actions[SUB_COLLAPSE]->setData(data);
    connect(actions[SUB_COLLAPSE], SIGNAL(triggered()), consoleTabulator, SLOT(splitCollapse()));
    menu_Actions->addAction(actions[SUB_COLLAPSE]);
    addAction(actions[SUB_COLLAPSE]);

    actions[SUB_NEXT] = new QAction(QIcon::fromTheme("go-up"), tr("N&ext Subterminal"), this);
    seq = QKeySequence::fromString( settings.value(SUB_NEXT, SUB_NEXT_SHORTCUT).toString() );
    actions[SUB_NEXT]->setShortcut(seq);
    actions[SUB_NEXT]->setData(data);
    connect(actions[SUB_NEXT], SIGNAL(triggered()), consoleTabulator, SLOT(switchNextSubterminal()));
    menu_Actions->addAction(actions[SUB_NEXT]);
    addAction(actions[SUB_NEXT]);

    actions[SUB_PREV] = new QAction(QIcon::fromTheme("go-down"), tr("P&revious Subterminal"), this);
    seq = QKeySequence::fromString( settings.value(SUB_PREV, SUB_PREV_SHORTCUT).toString() );
    actions[SUB_PREV]->setShortcut(seq);
    actions[SUB_PREV]->setData(data);
    connect(actions[SUB_PREV], SIGNAL(triggered()), consoleTabulator, SLOT(switchPrevSubterminal()));
    menu_Actions->addAction(actions[SUB_PREV]);
    addAction(actions[SUB_PREV]);

    menu_Actions->addSeparator();

    // Copy and Paste are only added to the table for the sake of bindings at the moment; there is no Edit menu, only a context menu.
    actions[COPY_SELECTION] = new QAction(QIcon::fromTheme("edit-copy"), tr("Copy &Selection"), this);
    seq = QKeySequence::fromString( settings.value(COPY_SELECTION, COPY_SELECTION_SHORTCUT).toString() );
    actions[COPY_SELECTION]->setShortcut(seq);
    connect(actions[COPY_SELECTION], SIGNAL(triggered()), consoleTabulator, SLOT(copySelection()));
    menu_Edit->addAction(actions[COPY_SELECTION]);
    addAction(actions[COPY_SELECTION]);

    actions[PASTE_CLIPBOARD] = new QAction(QIcon::fromTheme("edit-paste"), tr("Paste Clip&board"), this);
    seq = QKeySequence::fromString( settings.value(PASTE_CLIPBOARD, PASTE_CLIPBOARD_SHORTCUT).toString() );
    actions[PASTE_CLIPBOARD]->setShortcut(seq);
    connect(actions[PASTE_CLIPBOARD], SIGNAL(triggered()), consoleTabulator, SLOT(pasteClipboard()));
    menu_Edit->addAction(actions[PASTE_CLIPBOARD]);
    addAction(actions[PASTE_CLIPBOARD]);

    actions[PASTE_SELECTION] = new QAction(QIcon::fromTheme("edit-paste"), tr("Paste S&election"), this);
    seq = QKeySequence::fromString( settings.value(PASTE_SELECTION, PASTE_SELECTION_SHORTCUT).toString() );
    actions[PASTE_SELECTION]->setShortcut(seq);
    connect(actions[PASTE_SELECTION], SIGNAL(triggered()), consoleTabulator, SLOT(pasteSelection()));
    menu_Edit->addAction(actions[PASTE_SELECTION]);
    addAction(actions[PASTE_SELECTION]);

    actions[ZOOM_IN] = new QAction(QIcon::fromTheme("zoom-in"), tr("Zoom &in"), this);
    seq = QKeySequence::fromString( settings.value(ZOOM_IN, ZOOM_IN_SHORTCUT).toString() );
    actions[ZOOM_IN]->setShortcut(seq);
    connect(actions[ZOOM_IN], SIGNAL(triggered()), consoleTabulator, SLOT(zoomIn()));
    menu_Edit->addAction(actions[ZOOM_IN]);
    addAction(actions[ZOOM_IN]);

    actions[ZOOM_OUT] = new QAction(QIcon::fromTheme("zoom-out"), tr("Zoom &out"), this);
    seq = QKeySequence::fromString( settings.value(ZOOM_OUT, ZOOM_OUT_SHORTCUT).toString() );
    actions[ZOOM_OUT]->setShortcut(seq);
    connect(actions[ZOOM_OUT], SIGNAL(triggered()), consoleTabulator, SLOT(zoomOut()));
    menu_Edit->addAction(actions[ZOOM_OUT]);
    addAction(actions[ZOOM_OUT]);

    actions[ZOOM_RESET] = new QAction(QIcon::fromTheme("zoom-original"), tr("Zoom rese&t"), this);
    seq = QKeySequence::fromString( settings.value(ZOOM_RESET, ZOOM_RESET_SHORTCUT).toString() );
    actions[ZOOM_RESET]->setShortcut(seq);
    connect(actions[ZOOM_RESET], SIGNAL(triggered()), consoleTabulator, SLOT(zoomReset()));
    menu_Edit->addAction(actions[ZOOM_RESET]);
    addAction(actions[ZOOM_RESET]);

    menu_Actions->addSeparator();

    actions[FIND] = new QAction(QIcon::fromTheme("edit-find"), tr("&Find..."), this);
    seq = QKeySequence::fromString( settings.value(FIND, FIND_SHORTCUT).toString() );
    actions[FIND]->setShortcut(seq);
    connect(actions[FIND], SIGNAL(triggered()), this, SLOT(find()));
    menu_Actions->addAction(actions[FIND]);
    addAction(actions[FIND]);

#if 0
    act = new QAction(this);
    act->setSeparator(true);

    // TODO/FIXME: unimplemented for now
    act = new QAction(tr("&Save Session"), this);
    // do not use sequences for this task - it collides with eg. mc shorcuts
    // and mainly - it's not used too often
    //act->setShortcut(QKeySequence::Save);
    connect(act, SIGNAL(triggered()), consoleTabulator, SLOT(saveSession()));

    act = new QAction(tr("&Load Session"), this);
    // do not use sequences for this task - it collides with eg. mc shorcuts
    // and mainly - it's not used too often
    //act->setShortcut(QKeySequence::Open);
    connect(act, SIGNAL(triggered()), consoleTabulator, SLOT(loadSession()));
#endif

    actions[TOGGLE_MENU] = new QAction(tr("&Toggle Menu"), this);
    seq = QKeySequence::fromString( settings.value(TOGGLE_MENU, TOGGLE_MENU_SHORTCUT).toString() );
    actions[TOGGLE_MENU]->setShortcut(seq);
    connect(actions[TOGGLE_MENU], SIGNAL(triggered()), this, SLOT(toggleMenu()));
    addAction(actions[TOGGLE_MENU]);
    // this is correct - add action to main window - not to menu to keep toggle working

    // Add global rename current session shortcut
    actions[RENAME_SESSION] = new QAction(tr("Rename session"), this);
    seq = QKeySequence::fromString(settings.value(RENAME_SESSION, RENAME_SESSION_SHORTCUT).toString());
    actions[RENAME_SESSION]->setShortcut(seq);
    connect(actions[RENAME_SESSION], SIGNAL(triggered()), consoleTabulator, SLOT(renameCurrentSession()));
    addAction(actions[RENAME_SESSION]);
    // this is correct - add action to main window - not to menu

    settings.endGroup();
}
void MainWindow::setup_FileMenu_Actions()
{
    QSettings settings;
    settings.beginGroup("Shortcuts");

    QKeySequence seq;

    actions[ADD_TAB] = new QAction(QIcon::fromTheme("list-add"), tr("&New Tab"), this);
    seq = QKeySequence::fromString( settings.value(ADD_TAB, ADD_TAB_SHORTCUT).toString() );
    actions[ADD_TAB]->setShortcut(seq);
    connect(actions[ADD_TAB], SIGNAL(triggered()), this, SLOT(addNewTab()));
    menu_File->addAction(actions[ADD_TAB]);
    addAction(actions[ADD_TAB]);

    QMenu *presetsMenu = new QMenu(tr("New Tab From &Preset"), this);
    presetsMenu->addAction(QIcon(), tr("1 &Terminal"),
                           consoleTabulator, SLOT(addNewTab()));
    presetsMenu->addAction(QIcon(), tr("2 &Horizontal Terminals"),
                           consoleTabulator, SLOT(preset2Horizontal()));
    presetsMenu->addAction(QIcon(), tr("2 &Vertical Terminals"),
                           consoleTabulator, SLOT(preset2Vertical()));
    presetsMenu->addAction(QIcon(), tr("4 Terminal&s"),
                           consoleTabulator, SLOT(preset4Terminals()));
    menu_File->addMenu(presetsMenu);

    actions[CLOSE_TAB] = new QAction(QIcon::fromTheme("list-remove"), tr("&Close Tab"), this);
    seq = QKeySequence::fromString( settings.value(CLOSE_TAB, CLOSE_TAB_SHORTCUT).toString() );
    actions[CLOSE_TAB]->setShortcut(seq);
    connect(actions[CLOSE_TAB], SIGNAL(triggered()), consoleTabulator, SLOT(removeCurrentTab()));
    menu_File->addAction(actions[CLOSE_TAB]);
    addAction(actions[CLOSE_TAB]);

    actions[NEW_WINDOW] = new QAction(QIcon::fromTheme("window-new"), tr("&New Window"), this);
    seq = QKeySequence::fromString( settings.value(NEW_WINDOW, NEW_WINDOW_SHORTCUT).toString() );
    actions[NEW_WINDOW]->setShortcut(seq);
    connect(actions[NEW_WINDOW], SIGNAL(triggered()), this, SLOT(newTerminalWindow()));
    menu_File->addAction(actions[NEW_WINDOW]);
    addAction(actions[NEW_WINDOW]);

    menu_File->addSeparator();

    actions[PREFERENCES] = new QAction(tr("&Preferences..."), this);
    seq = QKeySequence::fromString( settings.value(PREFERENCES).toString() );
    actions[PREFERENCES]->setShortcut(seq);
    connect(actions[PREFERENCES], SIGNAL(triggered()), SLOT(actProperties_triggered()));
    menu_File->addAction(actions[PREFERENCES]);
    addAction(actions[PREFERENCES]);

    menu_File->addSeparator();

    actions[QUIT] = new QAction(QIcon::fromTheme("application-exit"), tr("&Quit"), this);
    seq = QKeySequence::fromString( settings.value(QUIT).toString() );
    actions[QUIT]->setShortcut(seq);
    connect(actions[QUIT], SIGNAL(triggered()), SLOT(close()));
    menu_File->addAction(actions[QUIT]);
    addAction(actions[QUIT]);

    settings.endGroup();
}

void MainWindow::setup_ViewMenu_Actions()
{
    QKeySequence seq;
    QSettings settings;
    settings.beginGroup("Shortcuts");

    QAction *hideBordersAction = new QAction(tr("&Hide Window Borders"), this);
    hideBordersAction->setCheckable(true);
    hideBordersAction->setVisible(!m_dropMode);
    seq = QKeySequence::fromString( settings.value(HIDE_WINDOW_BORDERS).toString() );
    hideBordersAction->setShortcut(seq);
    connect(hideBordersAction, SIGNAL(triggered()), this, SLOT(toggleBorderless()));
    menu_Window->addAction(hideBordersAction);
    addAction(hideBordersAction);
    actions[HIDE_WINDOW_BORDERS] = hideBordersAction;
    //actions[HIDE_WINDOW_BORDERS]->setObjectName("toggle_Borderless");
// TODO/FIXME: it's broken somehow. When I call toggleBorderless() here the non-responsive window appear
//    actions[HIDE_WINDOW_BORDERS]->setChecked(Properties::Instance()->borderless);
//    if (Properties::Instance()->borderless)
//        toggleBorderless();

    QAction *showTabBarAction = new QAction(tr("&Show Tab Bar"), this);
    //toggleTabbar->setObjectName("toggle_TabBar");
    showTabBarAction->setCheckable(true);
    showTabBarAction->setChecked(!Properties::Instance()->tabBarless);
    seq = QKeySequence::fromString( settings.value(SHOW_TAB_BAR).toString() );
    showTabBarAction->setShortcut(seq);
    menu_Window->addAction(showTabBarAction);
    addAction(showTabBarAction);
    actions[SHOW_TAB_BAR] = showTabBarAction;
    toggleTabBar();
    connect(showTabBarAction, SIGNAL(triggered()), this, SLOT(toggleTabBar()));

    QAction *toggleFullscreen = new QAction(tr("Fullscreen"), this);
    toggleFullscreen->setCheckable(true);
    toggleFullscreen->setChecked(false);
    seq = QKeySequence::fromString(settings.value(FULLSCREEN, FULLSCREEN_SHORTCUT).toString());
    toggleFullscreen->setShortcut(seq);
    menu_Window->addAction(toggleFullscreen);
    addAction(toggleFullscreen);
    connect(toggleFullscreen, SIGNAL(triggered(bool)), this, SLOT(showFullscreen(bool)));
    actions[FULLSCREEN] = toggleFullscreen;

    actions[TOGGLE_BOOKMARKS] = new QAction(tr("Toggle Bookmarks"), this);
    connect(actions[TOGGLE_BOOKMARKS], SIGNAL(triggered()), this, SLOT(toggleBookmarks()));
    seq = QKeySequence::fromString( settings.value(TOGGLE_BOOKMARKS, TOGGLE_BOOKMARKS_SHORTCUT).toString() );
    actions[TOGGLE_BOOKMARKS]->setShortcut(seq);
    menu_Window->addAction(actions[TOGGLE_BOOKMARKS]);
    settings.endGroup();

    menu_Window->addSeparator();

    /* tabs position */
    tabPosition = new QActionGroup(this);
    QAction *tabBottom = new QAction(tr("&Bottom"), this);
    QAction *tabTop = new QAction(tr("&Top"), this);
    QAction *tabRight = new QAction(tr("&Right"), this);
    QAction *tabLeft = new QAction(tr("&Left"), this);
    tabPosition->addAction(tabTop);
    tabPosition->addAction(tabBottom);
    tabPosition->addAction(tabLeft);
    tabPosition->addAction(tabRight);

    for(int i = 0; i < tabPosition->actions().size(); ++i)
        tabPosition->actions().at(i)->setCheckable(true);

    if( tabPosition->actions().count() > Properties::Instance()->tabsPos )
        tabPosition->actions().at(Properties::Instance()->tabsPos)->setChecked(true);

    connect(tabPosition, SIGNAL(triggered(QAction *)),
             consoleTabulator, SLOT(changeTabPosition(QAction *)) );

    tabPosMenu = new QMenu(tr("&Tabs Layout"), menu_Window);
    tabPosMenu->setObjectName("tabPosMenu");

    for(int i=0; i < tabPosition->actions().size(); ++i) {
        tabPosMenu->addAction(tabPosition->actions().at(i));
    }

    connect(menu_Window, SIGNAL(hovered(QAction *)),
            this, SLOT(updateActionGroup(QAction *)));
    menu_Window->addMenu(tabPosMenu);
    /* */

    /* Scrollbar */
    scrollBarPosition = new QActionGroup(this);
    QAction *scrollNone = new QAction(tr("&None"), this);
    QAction *scrollRight = new QAction(tr("&Right"), this);
    QAction *scrollLeft = new QAction(tr("&Left"), this);

    /* order of insertion is dep. on QTermWidget::ScrollBarPosition enum */
    scrollBarPosition->addAction(scrollNone);
    scrollBarPosition->addAction(scrollLeft);
    scrollBarPosition->addAction(scrollRight);

    for(int i = 0; i < scrollBarPosition->actions().size(); ++i)
        scrollBarPosition->actions().at(i)->setCheckable(true);

    if( Properties::Instance()->scrollBarPos < scrollBarPosition->actions().size() )
        scrollBarPosition->actions().at(Properties::Instance()->scrollBarPos)->setChecked(true);

    connect(scrollBarPosition, SIGNAL(triggered(QAction *)),
             consoleTabulator, SLOT(changeScrollPosition(QAction *)) );

    scrollPosMenu = new QMenu(tr("S&crollbar Layout"), menu_Window);
    scrollPosMenu->setObjectName("scrollPosMenu");

    for(int i=0; i < scrollBarPosition->actions().size(); ++i) {
        scrollPosMenu->addAction(scrollBarPosition->actions().at(i));
    }

    menu_Window->addMenu(scrollPosMenu);

    /* Keyboard cursor shape */
    keyboardCursorShape = new QActionGroup(this);
    QAction *block = new QAction(tr("&BlockCursor"), this);
    QAction *underline = new QAction(tr("&UnderlineCursor"), this);
    QAction *ibeam = new QAction(tr("&IBeamCursor"), this);

    /* order of insertion is dep. on QTermWidget::KeyboardCursorShape enum */
    keyboardCursorShape->addAction(block);
    keyboardCursorShape->addAction(underline);
    keyboardCursorShape->addAction(ibeam);

    for(int i = 0; i < keyboardCursorShape->actions().size(); ++i)
        keyboardCursorShape->actions().at(i)->setCheckable(true);

    if( Properties::Instance()->keyboardCursorShape < keyboardCursorShape->actions().size() )
        keyboardCursorShape->actions().at(Properties::Instance()->keyboardCursorShape)->setChecked(true);

    connect(keyboardCursorShape, SIGNAL(triggered(QAction *)),
             consoleTabulator, SLOT(changeKeyboardCursorShape(QAction *)) );

    keyboardCursorShapeMenu = new QMenu(tr("&Keyboard Cursor Shape"), menu_Window);
    keyboardCursorShapeMenu->setObjectName("keyboardCursorShapeMenu");

    for(int i=0; i < keyboardCursorShape->actions().size(); ++i) {
        keyboardCursorShapeMenu->addAction(keyboardCursorShape->actions().at(i));
    }

    menu_Window->addMenu(keyboardCursorShapeMenu);
}

void MainWindow::setupCustomDirs()
{
    const QSettings settings;
    const QString dir = QFileInfo(settings.fileName()).canonicalPath() + "/color-schemes/";
    TermWidgetImpl::addCustomColorSchemeDir(dir);
}

void MainWindow::on_consoleTabulator_currentChanged(int)
{
}

void MainWindow::toggleTabBar()
{
    Properties::Instance()->tabBarless
            = !actions[SHOW_TAB_BAR]->isChecked();
    consoleTabulator->showHideTabBar();
}

void MainWindow::toggleBorderless()
{
    setWindowFlags(windowFlags() ^ Qt::FramelessWindowHint);
    show();
    setWindowState(Qt::WindowActive); /* don't loose focus on the window */
    Properties::Instance()->borderless
            = actions[HIDE_WINDOW_BORDERS]->isChecked(); realign();
}

void MainWindow::toggleMenu()
{
    m_menuBar->setVisible(!m_menuBar->isVisible());
    Properties::Instance()->menuVisible = m_menuBar->isVisible();
}

void MainWindow::showFullscreen(bool fullscreen)
{
    if(fullscreen)
        setWindowState(windowState() | Qt::WindowFullScreen);
    else
        setWindowState(windowState() & ~Qt::WindowFullScreen);
}

void MainWindow::toggleBookmarks()
{
    m_bookmarksDock->toggleViewAction()->trigger();
}


void MainWindow::closeEvent(QCloseEvent *ev)
{
    if (!Properties::Instance()->askOnExit
            || !consoleTabulator->count())
    {
        // #80 - do not save state and geometry in drop mode
        if (!m_dropMode) {
            if (Properties::Instance()->savePosOnExit) {
            	Properties::Instance()->mainWindowPosition = pos();
            }
            if (Properties::Instance()->saveSizeOnExit) {
            	Properties::Instance()->mainWindowSize = size();
            }
            Properties::Instance()->mainWindowState = saveState();
        }
        Properties::Instance()->saveSettings();
        for (int i = consoleTabulator->count(); i > 0; --i) {
            consoleTabulator->removeTab(i - 1);
        }
        ev->accept();
        return;
    }

    // ask user for cancel only when there is at least one terminal active in this window
    QDialog * dia = new QDialog(this);
    dia->setObjectName("exitDialog");
    dia->setWindowTitle(tr("Exit QTerminal"));

    QCheckBox * dontAskCheck = new QCheckBox(tr("Do not ask again"), dia);
    QDialogButtonBox * buttonBox = new QDialogButtonBox(QDialogButtonBox::Yes | QDialogButtonBox::No, Qt::Horizontal, dia);
    buttonBox->button(QDialogButtonBox::Yes)->setDefault(true);

    connect(buttonBox, SIGNAL(accepted()), dia, SLOT(accept()));
    connect(buttonBox, SIGNAL(rejected()), dia, SLOT(reject()));

    QVBoxLayout * lay = new QVBoxLayout();
    lay->addWidget(new QLabel(tr("Are you sure you want to exit?")));
    lay->addWidget(dontAskCheck);
    lay->addWidget(buttonBox);
    dia->setLayout(lay);

    if (dia->exec() == QDialog::Accepted) {
        Properties::Instance()->mainWindowPosition = pos();
        Properties::Instance()->mainWindowSize = size();
        Properties::Instance()->mainWindowState = saveState();
        Properties::Instance()->askOnExit = !dontAskCheck->isChecked();
        Properties::Instance()->saveSettings();
        for (int i = consoleTabulator->count(); i > 0; --i) {
            consoleTabulator->removeTab(i - 1);
        }
        ev->accept();
    } else {
        ev->ignore();
    }

    dia->deleteLater();
}

void MainWindow::actAbout_triggered()
{
    QMessageBox::about(this, QString("QTerminal ") + STR_VERSION, tr("A lightweight multiplatform terminal emulator"));
}

void MainWindow::actProperties_triggered()
{
    PropertiesDialog *p = new PropertiesDialog(this);
    connect(p, SIGNAL(propertiesChanged()), this, SLOT(propertiesChanged()));
    p->exec();
}

void MainWindow::propertiesChanged()
{
    rebuildActions();

    QApplication::setStyle(Properties::Instance()->guiStyle);
    setWindowOpacity(1.0 - Properties::Instance()->appTransparency/100.0);
    consoleTabulator->setTabPosition((QTabWidget::TabPosition)Properties::Instance()->tabsPos);
    consoleTabulator->propertiesChanged();
    setDropShortcut(Properties::Instance()->dropShortCut);

    m_menuBar->setVisible(Properties::Instance()->menuVisible);

    m_bookmarksDock->setVisible(Properties::Instance()->useBookmarks
                                && Properties::Instance()->bookmarksVisible);
    m_bookmarksDock->toggleViewAction()->setVisible(Properties::Instance()->useBookmarks);

    if (Properties::Instance()->useBookmarks)
    {
        qobject_cast<BookmarksWidget*>(m_bookmarksDock->widget())->setup();
    }

    onCurrentTitleChanged(consoleTabulator->currentIndex());

    realign();
}

void MainWindow::realign()
{
    if (m_dropMode)
    {
        QRect desktop = QApplication::desktop()->availableGeometry(this);
        QRect geometry = QRect(0, 0,
                               desktop.width()  * Properties::Instance()->dropWidht  / 100,
                               desktop.height() * Properties::Instance()->dropHeight / 100
                              );
        geometry.moveCenter(desktop.center());
        // do not use 0 here - we need to calculate with potential panel on top
        geometry.setTop(desktop.top());

        setGeometry(geometry);
    }
}

void MainWindow::updateActionGroup(QAction *a)
{
    if (a->parent()->objectName() == tabPosMenu->objectName()) {
        tabPosition->actions().at(Properties::Instance()->tabsPos)->setChecked(true);
    }
}

void MainWindow::showHide()
{
    if (isVisible())
        hide();
    else
    {
       realign();
       show();
       activateWindow();
    }
}

void MainWindow::setKeepOpen(bool value)
{
    Properties::Instance()->dropKeepOpen = value;
    if (!m_dropLockButton)
        return;

    if (value)
        m_dropLockButton->setIcon(QIcon::fromTheme("object-locked"));
    else
        m_dropLockButton->setIcon(QIcon::fromTheme("object-unlocked"));

    m_dropLockButton->setChecked(value);
}

void MainWindow::find()
{
    // A bit ugly perhaps with 4 levels of indirection...
    consoleTabulator->terminalHolder()->currentTerminal()->impl()->toggleShowSearchBar();
}


bool MainWindow::event(QEvent *event)
{
    if (event->type() == QEvent::WindowDeactivate)
    {
        if (m_dropMode &&
            !Properties::Instance()->dropKeepOpen &&
            qApp->activeWindow() == 0
           )
           hide();
    }
    return QMainWindow::event(event);
}

void MainWindow::newTerminalWindow()
{
    MainWindow *w = new MainWindow(m_initWorkDir, m_initShell, false);
    w->show();
}

void MainWindow::bookmarksWidget_callCommand(const QString& cmd)
{
    consoleTabulator->terminalHolder()->currentTerminal()->impl()->sendText(cmd);
    consoleTabulator->terminalHolder()->currentTerminal()->setFocus();
}

void MainWindow::bookmarksDock_visibilityChanged(bool visible)
{
    Properties::Instance()->bookmarksVisible = visible;
}

void MainWindow::addNewTab()
{
    if (Properties::Instance()->terminalsPreset == 3)
        consoleTabulator->preset4Terminals();
    else if (Properties::Instance()->terminalsPreset == 2)
        consoleTabulator->preset2Vertical();
    else if (Properties::Instance()->terminalsPreset == 1)
        consoleTabulator->preset2Horizontal();
    else
        consoleTabulator->addNewTab();
}

void MainWindow::onCurrentTitleChanged(int index)
{
    QString title;
    QIcon icon;
    if (-1 != index)
    {
        title = consoleTabulator->tabText(index);
        icon = consoleTabulator->tabIcon(index);
    }
    setWindowTitle(title.isEmpty() || !Properties::Instance()->changeWindowTitle ? QStringLiteral("QTerminal") : title);
    setWindowIcon(icon.isNull() || !Properties::Instance()->changeWindowIcon ? QIcon::fromTheme("utilities-terminal") : icon);
}

bool MainWindow::hasMultipleTabs()
{
    return consoleTabulator->findChildren<TermWidgetHolder*>().count() > 1;
}

bool MainWindow::hasMultipleSubterminals()
{
    return consoleTabulator->terminalHolder()->findChildren<TermWidget*>().count() > 1;
}

void MainWindow::aboutToShowActionsMenu()
{
    const QList<QAction*> actions = menu_Actions->actions();
    for (QAction *action : actions) {
        if (!action->data().isNull()) {
            const checkfn check = action->data().value<checkfn>();
            action->setEnabled(check(*this));
        }
    }
}


QMap< QString, QAction * >& MainWindow::leaseActions() {
        return actions;
}
