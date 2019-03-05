/***************************************************************************
 *   Copyright 2009 by Martin Gräßlin <kde@martin-graesslin.com>           *
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
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA .        *
 ***************************************************************************/
#include "switcher.h"

#include "config-switcher.h"

#include <QTimer>

#include <QDebug>
#include <QIcon>
#include <KWindowSystem>
#include <KLocalizedString>
#include <QCursor>


#ifdef HAVE_X11

#include <QtX11Extras/QX11Info>
#include <netwm.h>

#endif

#include<QtDebug>
#include <QtCore/QDebug>

K_EXPORT_PLASMA_RUNNER(windows, Switcher)

Switcher::Switcher(QObject *parent, const QVariantList &args)
        : AbstractRunner(parent, args),
          m_inSession(false),
          m_ready(false) {
    Q_UNUSED(args);
    setObjectName(QLatin1String("Switcher"));


    setDefaultSyntax(Plasma::RunnerSyntax(i18nc("Note this is a KRunner keyword", ".<app name>"),
                                          i18n("Switch to application by typing a dot and the app name, e.g. '.emacs'")));

    connect(this, &Plasma::AbstractRunner::prepare, this, &Switcher::prepareForMatchSession);
    connect(this, &Plasma::AbstractRunner::teardown, this, &Switcher::matchSessionComplete);
}

Switcher::~Switcher() {
}

// Called in the main thread
void Switcher::gatherInfo() {
    QMutexLocker locker(&m_mutex);
    if (!m_inSession) {
        return;
    }

            foreach (const WId w, KWindowSystem::windows()) {
            KWindowInfo info(w, NET::WMWindowType | NET::WMDesktop |
                                NET::WMState | NET::XAWMState |
                                NET::WMName,
                             NET::WM2WindowClass | NET::WM2WindowRole | NET::WM2AllowedActions);
            if (info.valid()) {
                // ignore NET::Tool and other special window types
                NET::WindowType wType = info.windowType(NET::NormalMask | NET::DesktopMask | NET::DockMask |
                                                        NET::ToolbarMask | NET::MenuMask | NET::DialogMask |
                                                        NET::OverrideMask | NET::TopMenuMask |
                                                        NET::UtilityMask | NET::SplashMask);

                if (wType != NET::Normal && wType != NET::Override && wType != NET::Unknown &&
                    wType != NET::Dialog && wType != NET::Utility) {
                    continue;
                }
                m_windows.insert(w, info);
                m_icons.insert(w, QIcon(KWindowSystem::icon(w)));
            }
        }

    for (int i = 1; i <= KWindowSystem::numberOfDesktops(); i++) {
        m_desktopNames << KWindowSystem::desktopName(i);
    }

    m_ready = true;
}

// Called in the main thread
void Switcher::prepareForMatchSession() {
    QMutexLocker locker(&m_mutex);
    m_inSession = true;
    m_ready = false;
    QTimer::singleShot(0, this, &Switcher::gatherInfo);
}

// Called in the main thread
void Switcher::matchSessionComplete() {
    QMutexLocker locker(&m_mutex);
    m_inSession = false;
    m_ready = false;
    m_desktopNames.clear();
    m_icons.clear();
    m_windows.clear();
}

// Called in the secondary thread
void Switcher::match(Plasma::RunnerContext &context) {
    QMutexLocker locker(&m_mutex);
    if (!m_ready) {
        return;
    }

    QString term = context.query();

    QList<Plasma::QueryMatch> matches;

    // keyword match: when term starts with "window" we list all windows
    // the list can be restricted to windows matching a given name, class, role or desktop
    if (term.startsWith(i18nc("Note this is a KRunner keyword", "."), Qt::CaseInsensitive)) {
        QString windowName = term.mid(1, term.size());

        QHashIterator<WId, KWindowInfo> it(m_windows);
        while (it.hasNext()) {
            it.next();
            WId w = it.key();
            KWindowInfo info = it.value();
            QString windowClass = QString::fromUtf8(info.windowClassName());

            // exclude not matching windows
            if (!KWindowSystem::hasWId(w)) {
                continue;
            }
            if (!windowName.isEmpty() && !info.name().contains(windowName, Qt::CaseInsensitive) &&
                !windowClass.contains(windowName, Qt::CaseInsensitive)) {
                continue;
            }
            matches << windowMatch(info);
        }

        if (!matches.isEmpty()) {
            // the window keyword found matches - do not process other syntax possibilities
            context.addMatches(matches);
            return;
        }
    }


    if (!matches.isEmpty()) {
        context.addMatches(matches);
    }
}

// Called in the main thread
void Switcher::run(const Plasma::RunnerContext &context, const Plasma::QueryMatch &match) {
    QMutexLocker locker(&m_mutex);
    Q_UNUSED(context)
    // check if it's a desktop
    if (match.id().startsWith(QLatin1String("windows_desktop"))) {
        KWindowSystem::setCurrentDesktop(match.data().toInt());
        return;
    }

    WId w(match.data().toString().toULong());
    //this is needed since KWindowInfo() doesn't exist, m_windows[w] doesn't work
    QHash<WId, KWindowInfo>::iterator i = m_windows.find(w);
    KWindowInfo info = i.value();

    KWindowSystem::forceActiveWindow(w);
    QCursor c = QCursor();
    KWindowInfo focusWindow(w, NET::WMGeometry);
    c.setPos(focusWindow.geometry().center());
}


Plasma::QueryMatch Switcher::windowMatch(const KWindowInfo &info, qreal relevance, Plasma::QueryMatch::Type type) {
    Plasma::QueryMatch match(this);
    match.setType(type);
    match.setData(QString::number(info.win()));
    match.setIcon(m_icons[info.win()]);
    match.setText(info.name());
    QString desktopName;
    int desktop = info.desktop();
    if (desktop == NET::OnAllDesktops) {
        desktop = KWindowSystem::currentDesktop();
    }
    if (desktop <= m_desktopNames.size()) {
        desktopName = m_desktopNames[desktop - 1];
    } else {
        desktopName = KWindowSystem::desktopName(desktop);
    }

    match.setSubtext(i18n("Activate running window on %1", desktopName));
    match.setRelevance(relevance);
    return match;
}


#include "switcher.moc"
