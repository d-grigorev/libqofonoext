/****************************************************************************
**
** Copyright (C) 2015 Jolla Ltd.
** Contact: slava.monich@jollamobile.com
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
****************************************************************************/

#include "qofonoextmodemmanager.h"
#include "qofonoext_p.h"

#include "modemmanager_interface.h"

class QOfonoExtModemManager::Private : public QObject
{
    Q_OBJECT

public:
    static QWeakPointer<QOfonoExtModemManager> sSharedInstance;

    QOfonoExtModemManager* iParent;
    QOfonoExtModemManagerProxy* iProxy;
    QStringList iAvailableModems;
    QStringList iEnabledModems;
    QString iDefaultVoiceModem;
    QString iDefaultDataModem;
    QString iDefaultVoiceSim;
    QString iDefaultDataSim;
    QList<bool> iPresentSims;
    int iPresentSimCount;
    int iActiveSimCount;
    bool iValid;

    Private(QOfonoExtModemManager* aParent);

    static QStringList toStringList(QList<QDBusObjectPath> aList);
    static QList<QDBusObjectPath> toPathList(QStringList aList);

    void presentSimsChanged(QList<bool> aOldList);
    void updateSimCounts();

private Q_SLOTS:
    void onServiceRegistered();
    void onServiceUnregistered();
    void onGetAllFinished(QDBusPendingCallWatcher* aWatcher);
    void onEnabledModemsChanged(QList<QDBusObjectPath> aModems);
    void onDefaultVoiceModemChanged(QString aModemPath);
    void onDefaultDataModemChanged(QString aModemPath);
    void onDefaultVoiceSimChanged(QString aImsi);
    void onDefaultDataSimChanged(QString aImsi);
    void onPresentSimsChanged(int aIndex, bool aPresent);
};

QWeakPointer<QOfonoExtModemManager> QOfonoExtModemManager::Private::sSharedInstance;

QStringList QOfonoExtModemManager::Private::toStringList(QList<QDBusObjectPath> aList)
{
    QStringList stringList;
    const int n = aList.count();
    for (int i=0; i<n; i++) {
        stringList.append(aList.at(i).path());
    }
    return stringList;
}

QList<QDBusObjectPath> QOfonoExtModemManager::Private::toPathList(QStringList aList)
{
    QList<QDBusObjectPath> pathList;
    const int n = aList.count();
    for (int i=0; i<n; i++) {
        pathList.append(QDBusObjectPath(aList.at(i)));
    }
    return pathList;
}

QOfonoExtModemManager::Private::Private(QOfonoExtModemManager* aParent) :
    QObject(aParent),
    iParent(aParent),
    iProxy(NULL),
    iPresentSimCount(0),
    iActiveSimCount(0),
    iValid(false)
{
    QDBusServiceWatcher* ofonoWatcher = new QDBusServiceWatcher(OFONO_SERVICE,
        OFONO_BUS, QDBusServiceWatcher::WatchForRegistration |
        QDBusServiceWatcher::WatchForUnregistration, this);

    connect(ofonoWatcher, SIGNAL(serviceRegistered(QString)),
        this, SLOT(onServiceRegistered()));
    connect(ofonoWatcher, SIGNAL(serviceUnregistered(QString)),
        this, SLOT(onServiceUnregistered()));

    if (OFONO_BUS.interface()->isServiceRegistered(OFONO_SERVICE)) {
        onServiceRegistered();
    }
}

void QOfonoExtModemManager::Private::onServiceRegistered()
{
    const bool wasValid = iValid;
    if (!iProxy) {
        iProxy = new QOfonoExtModemManagerProxy(OFONO_SERVICE, "/", OFONO_BUS, this);
        if (iProxy->isValid()) {
            iValid = false;
            connect(new QDBusPendingCallWatcher(iProxy->GetAll2(), iProxy),
                SIGNAL(finished(QDBusPendingCallWatcher*)),
                SLOT(onGetAllFinished(QDBusPendingCallWatcher*)));
            connect(iProxy,
                SIGNAL(EnabledModemsChanged(QList<QDBusObjectPath>)),
                SLOT(onEnabledModemsChanged(QList<QDBusObjectPath>)));
            connect(iProxy,
                SIGNAL(DefaultDataModemChanged(QString)),
                SLOT(onDefaultDataModemChanged(QString)));
            connect(iProxy,
                SIGNAL(DefaultVoiceModemChanged(QString)),
                SLOT(onDefaultVoiceModemChanged(QString)));
            connect(iProxy,
                SIGNAL(DefaultDataSimChanged(QString)),
                SLOT(onDefaultDataSimChanged(QString)));
            connect(iProxy,
                SIGNAL(DefaultVoiceSimChanged(QString)),
                SLOT(onDefaultVoiceSimChanged(QString)));
            connect(iProxy,
                SIGNAL(PresentSimsChanged(int,bool)),
                SLOT(onPresentSimsChanged(int,bool)));
        } else {
            delete iProxy;
            iProxy = NULL;
        }
    }
    if (wasValid != iValid) {
        Q_EMIT iParent->validChanged(iValid);
    }
}

void QOfonoExtModemManager::Private::onServiceUnregistered()
{
    if (iValid) {
        iValid = false;
        Q_EMIT iParent->validChanged(iValid);
    }
    if (iProxy) {
        delete iProxy;
        iProxy = NULL;
    }
}

void QOfonoExtModemManager::Private::onGetAllFinished(QDBusPendingCallWatcher* aWatcher)
{
    QDBusPendingReply<int,      // InterfaceVersion
        QList<QDBusObjectPath>, // AvailableModems
        QList<QDBusObjectPath>, // EnabledModems
        QString,                // DefaultDataSim
        QString,                // DefaultVoiceSim
        QString,                // DefaultDataModem
        QString,                // DefaultVoiceModem
        QOfonoExtBoolList>      // PresentSims
        reply(*aWatcher);
    if (reply.isError()) {
        qWarning() << reply.error();
    } else {
        QStringList list = toStringList(reply.argumentAt<1>());
        if (iAvailableModems != list) {
            iAvailableModems = list;
            Q_EMIT iParent->availableModemsChanged(iAvailableModems);
        }
        list = toStringList(reply.argumentAt<2>());
        if (iEnabledModems != list) {
            iEnabledModems = list;
            Q_EMIT iParent->enabledModemsChanged(iEnabledModems);
        }
        QString imsi = reply.argumentAt<3>();
        if (iDefaultDataSim != imsi) {
            iDefaultDataSim = imsi;
            Q_EMIT iParent->defaultDataSimChanged(iDefaultDataSim);
        }
        imsi = reply.argumentAt<4>();
        if (iDefaultVoiceSim != imsi) {
            iDefaultVoiceSim = imsi;
            Q_EMIT iParent->defaultVoiceSimChanged(iDefaultVoiceSim);
        }
        QString path = reply.argumentAt<5>();
        if (iDefaultDataModem != path) {
            iDefaultDataModem = path;
            Q_EMIT iParent->defaultDataModemChanged(iDefaultDataModem);
        }
        path = reply.argumentAt<6>();
        if (iDefaultVoiceModem != path) {
            iDefaultVoiceModem = path;
            Q_EMIT iParent->defaultVoiceModemChanged(iDefaultVoiceModem);
        }

        QList<bool> oldList = iPresentSims;
        iPresentSims = (QList<bool>)reply.argumentAt<7>();
        presentSimsChanged(oldList);

        if (!iValid) {
            iValid = true;
            Q_EMIT iParent->validChanged(iValid);
        }
    }
    aWatcher->deleteLater();
}

void QOfonoExtModemManager::Private::presentSimsChanged(QList<bool> aOldList)
{
    int i;
    const int n = iPresentSims.count();
    QList<bool> changed;
    changed.reserve(n);
    for (i=0; i<n; i++) {
        const bool prev = (i<aOldList.count() && aOldList.at(i));
        changed.append(iPresentSims.at(i) != prev);
    }
    updateSimCounts();
    for (i=0; i<n; i++) {
        if (changed.at(i)) {
            Q_EMIT iParent->presentSimChanged(i, iPresentSims.at(i));
        }
    }
    if (aOldList != iPresentSims) {
        Q_EMIT iParent->presentSimsChanged(iPresentSims);
    }
}

void QOfonoExtModemManager::Private::updateSimCounts()
{
    const int oldPresentSimCount = iPresentSimCount;
    const int oldActiveSimCount = iActiveSimCount;

    iPresentSimCount = 0;
    iActiveSimCount = 0;

    const int n = iPresentSims.count();
    for (int i=0; i<n; i++) {
        if (iPresentSims.at(i)) {
            iPresentSimCount++;
            if (i < iAvailableModems.count() &&
                iEnabledModems.contains(iAvailableModems.at(i))) {
                iActiveSimCount++;
            }
        }
    }
    if (oldPresentSimCount != iPresentSimCount) {
        Q_EMIT iParent->presentSimCountChanged(iPresentSimCount);
    }
    if (oldActiveSimCount != iActiveSimCount) {
        Q_EMIT iParent->activeSimCountChanged(iActiveSimCount);
    }
}

void QOfonoExtModemManager::Private::onEnabledModemsChanged(QList<QDBusObjectPath> aModems)
{
    QStringList enabledModems = toStringList(aModems);
    if (iEnabledModems != enabledModems) {
        iEnabledModems = enabledModems;
        Q_EMIT iParent->enabledModemsChanged(iEnabledModems);
    }
    updateSimCounts();
}

void QOfonoExtModemManager::Private::onDefaultDataModemChanged(QString aPath)
{
    iDefaultDataModem = aPath;
    Q_EMIT iParent->defaultDataModemChanged(aPath);
}

void QOfonoExtModemManager::Private::onDefaultVoiceModemChanged(QString aPath)
{
    iDefaultVoiceModem = aPath;
    Q_EMIT iParent->defaultVoiceModemChanged(aPath);
}

void QOfonoExtModemManager::Private::onDefaultDataSimChanged(QString aImsi)
{
    if (iDefaultDataSim != aImsi) {
        iDefaultDataSim = aImsi;
        Q_EMIT iParent->defaultDataSimChanged(aImsi);
    }
}

void QOfonoExtModemManager::Private::onDefaultVoiceSimChanged(QString aImsi)
{
    if (iDefaultVoiceSim != aImsi) {
        iDefaultVoiceSim = aImsi;
        Q_EMIT iParent->defaultVoiceSimChanged(aImsi);
    }
}

void QOfonoExtModemManager::Private::onPresentSimsChanged(int aIndex, bool aPresent)
{
    if (aIndex >= 0 && aIndex < iPresentSims.count()) {
        QList<bool> oldList = iPresentSims;
        iPresentSims[aIndex] = aPresent;
        presentSimsChanged(oldList);
    }
}

// ==========================================================================
// QOfonoExtModemManager
// ==========================================================================

QOfonoExtModemManager::QOfonoExtModemManager(QObject* aParent) :
    QObject(aParent),
    iPrivate(new Private(this))
{
}

QOfonoExtModemManager::~QOfonoExtModemManager()
{
}

bool QOfonoExtModemManager::valid() const
{
    return iPrivate->iValid;
}

QStringList QOfonoExtModemManager::availableModems() const
{
    return iPrivate->iAvailableModems;
}

QStringList QOfonoExtModemManager::enabledModems() const
{
    return iPrivate->iEnabledModems;
}

QString QOfonoExtModemManager::defaultVoiceModem() const
{
    return iPrivate->iDefaultVoiceModem;
}

QString QOfonoExtModemManager::defaultDataModem() const
{
    return iPrivate->iDefaultDataModem;
}

QString QOfonoExtModemManager::defaultVoiceSim() const
{
    return iPrivate->iDefaultVoiceSim;
}

QString QOfonoExtModemManager::defaultDataSim() const
{
    return iPrivate->iDefaultDataSim;
}

QList<bool> QOfonoExtModemManager::presentSims() const
{
    return iPrivate->iPresentSims;
}

int QOfonoExtModemManager::presentSimCount() const
{
    return iPrivate->iPresentSimCount;
}

int QOfonoExtModemManager::activeSimCount() const
{
    return iPrivate->iActiveSimCount;
}

bool QOfonoExtModemManager::simPresentAt(int aIndex) const
{
    if (aIndex >= 0 && aIndex < iPrivate->iPresentSims.count()) {
        return iPrivate->iPresentSims.at(aIndex);
    } else {
        return false;
    }
}

void QOfonoExtModemManager::setEnabledModems(QStringList aModems)
{
    if (iPrivate->iProxy) {
        iPrivate->iProxy->SetEnabledModems(Private::toPathList(aModems));
    }
    // Optimistically cache the changes
    if (iPrivate->iEnabledModems != aModems) {
        iPrivate->iEnabledModems = aModems;
        Q_EMIT enabledModemsChanged(aModems);
    }
}

void QOfonoExtModemManager::setDefaultDataSim(QString aImsi)
{
    if (iPrivate->iProxy) {
        iPrivate->iProxy->SetDefaultDataSim(aImsi);
    }
    // Optimistically cache the changes
    if (iPrivate->iDefaultDataSim != aImsi) {
        iPrivate->iDefaultDataSim = aImsi;
        Q_EMIT defaultDataSimChanged(aImsi);
    }
}

void QOfonoExtModemManager::setDefaultVoiceSim(QString aImsi)
{
    if (iPrivate->iProxy) {
        iPrivate->iProxy->SetDefaultVoiceSim(aImsi);
    }
    // Optimistically cache the changes
    if (iPrivate->iDefaultVoiceSim != aImsi) {
        iPrivate->iDefaultVoiceSim = aImsi;
        Q_EMIT defaultVoiceSimChanged(aImsi);
    }
}

QSharedPointer<QOfonoExtModemManager> QOfonoExtModemManager::instance()
{
    QSharedPointer<QOfonoExtModemManager> instance = Private::sSharedInstance;
    if (instance.isNull()) {
        instance = QSharedPointer<QOfonoExtModemManager>::create();
        Private::sSharedInstance = instance;
    }
    return instance;
}

#include "qofonoextmodemmanager.moc"
