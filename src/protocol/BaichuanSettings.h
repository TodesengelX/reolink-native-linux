#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace rl::bcsettings {

enum class ChannelScope { Channel, Host };

struct Schema {
    quint32 getCommand = 0;
    quint32 setCommand = 0;
    QString name;
    QByteArray requestBody;
    ChannelScope scope = ChannelScope::Channel;
    QStringList responseTags;
    bool expose = false;
    const char *provenance = "observed wire behavior";
};

struct Result {
    bool ok = false;
    bool transport = false;
    bool protocol = false;
    quint16 status = 0;
    QVariantMap fields;
    QVariantMap unknownFields;
    QString error;
};

struct WriteResult {
    bool ok = false;
    bool transport = false;
    bool auth = false;
    bool locked = false;
    bool protocol = false;
    bool deviceRejected = false;
    quint16 status = 0;
    QString error;
};

const Schema *schemaFor(quint32 getCommand);
Result parse(const QByteArray &xml, quint16 status, const Schema &schema);
bool validateChanges(quint32 getCommand, const QVariantMap &changes, QString *error);

} // namespace rl::bcsettings