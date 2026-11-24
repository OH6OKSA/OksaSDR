#pragma once

#include "core/discovery/RadioDiscoverySource.h"

#include <QAbstractListModel>
#include <QList>
#include <QtQml/qqmlregistration.h>

namespace AetherSDR::mobile {

// Radios reported by a RadioDiscoverySource, keyed by (family, serial).
class RadioListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by MobileEngine")
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Role {
        FamilyRole = Qt::UserRole + 1,
        SerialRole,
        NameRole,
        ModelRole,
        NicknameRole,
        VersionRole,
        TransportRole,
        AddressRole,
        InUseRole,
    };

    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    const DiscoveredRadio* radioAt(int row) const;
    // The row of a radio, -1 when it is not on the list (the engine logs a
    // radio's first appearance and its going: roadmap 166).
    int indexOf(const QString& family, const QString& serial) const;

    void upsert(const DiscoveredRadio& radio);
    void remove(const QString& family, const QString& serial);

signals:
    void countChanged();

private:
    QList<DiscoveredRadio> m_radios;
};

} // namespace AetherSDR::mobile
