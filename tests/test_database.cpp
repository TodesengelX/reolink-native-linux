#include "core/Database.h"

#include <QTemporaryDir>
#include <QtTest>

using namespace rl;

class TestDatabase : public QObject
{
    Q_OBJECT

    // Current schema version; bump alongside new migrations in Database.cpp.
    static constexpr int kSchemaVersion = 2;

private slots:
    void migratesToCurrent()
    {
        QTemporaryDir dir;
        Database db(dir.filePath("t.db"), QStringLiteral("test_migrate"));
        QVERIFY2(db.open(), qPrintable(db.lastError()));
        QCOMPARE(db.schemaVersion(), kSchemaVersion);
    }

    void openIsIdempotent()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("t.db");
        {
            Database db(path, QStringLiteral("test_idem_1"));
            QVERIFY(db.open());
        }
        Database db2(path, QStringLiteral("test_idem_2"));
        QVERIFY(db2.open());
        QCOMPARE(db2.schemaVersion(), kSchemaVersion);
    }

    void eventCrud()
    {
        QTemporaryDir dir;
        Database db(dir.filePath("t.db"), QStringLiteral("test_events"));
        QVERIFY(db.open());
        EventRecord e;
        e.hostId = 7;
        e.timestamp = 1700000000;
        e.type = QStringLiteral("person");
        e.camera = QStringLiteral("Front Door");
        QVERIFY(db.addEvent(e) > 0);
        e.timestamp = 1700000100;
        e.type = QStringLiteral("vehicle");
        QVERIFY(db.addEvent(e) > 0);

        const QVector<EventRecord> evs = db.recentEvents(10);
        QCOMPARE(evs.size(), 2);
        QCOMPARE(evs[0].type, QStringLiteral("vehicle")); // newest first
        QCOMPARE(evs[1].type, QStringLiteral("person"));
        QVERIFY(db.clearEvents());
        QCOMPARE(db.recentEvents(10).size(), 0);
    }

    void hostCrud()
    {
        QTemporaryDir dir;
        Database db(dir.filePath("t.db"), QStringLiteral("test_crud"));
        QVERIFY(db.open());

        HostRecord rec;
        rec.kind = QStringLiteral("camera");
        rec.name = QStringLiteral("Front Door");
        rec.addr = QStringLiteral("192.168.1.20");
        rec.port = 443;
        rec.https = true;
        rec.username = QStringLiteral("admin");
        const qint64 id = db.addHost(rec);
        QVERIFY2(id > 0, qPrintable(db.lastError()));

        QVector<HostRecord> hosts = db.hosts();
        QCOMPARE(hosts.size(), 1);
        QCOMPARE(hosts[0].name, QStringLiteral("Front Door"));
        QCOMPARE(hosts[0].https, true);

        hosts[0].name = QStringLiteral("Back Door");
        hosts[0].model = QStringLiteral("RLC-810A");
        QVERIFY(db.updateHost(hosts[0]));
        QCOMPARE(db.hosts()[0].name, QStringLiteral("Back Door"));
        QCOMPARE(db.hosts()[0].model, QStringLiteral("RLC-810A"));

        QVERIFY(db.removeHost(id));
        QCOMPARE(db.hosts().size(), 0);
    }
};

QTEST_GUILESS_MAIN(TestDatabase)
#include "test_database.moc"
