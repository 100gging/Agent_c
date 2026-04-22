#include "rankingmanager.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>
#include <algorithm>

RankingManager::RankingManager(const QString &dir, QObject *parent)
    : QObject(parent), m_dir(dir)
{
    QDir().mkpath(m_dir);
    load(Single);
    load(Competitive);
    load(Cooperative);
}

QString RankingManager::filePath(Mode mode) const
{
    switch (mode) {
    case Single:      return m_dir + "/ranking_single.json";
    case Competitive: return m_dir + "/ranking_competitive.json";
    case Cooperative: return m_dir + "/ranking_cooperative.json";
    }
    return m_dir + "/ranking_single.json";
}

void RankingManager::load(Mode mode)
{
    QFile file(filePath(mode));
    QVector<Entry> entries;

    if (file.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();

        QJsonArray arr = doc.array();
        for (const QJsonValue &v : arr) {
            QJsonObject obj = v.toObject();
            Entry e;
            e.score      = obj["score"].toInt();
            e.photoPath  = obj["photo"].toString();
            e.photoPath2 = obj["photo2"].toString();
            e.timestamp  = (qint64)obj["timestamp"].toDouble();
            entries.append(e);
        }
    }

    // 내림차순 정렬, 상위 3개만 유지
    std::sort(entries.begin(), entries.end(),
              [](const Entry &a, const Entry &b) { return a.score > b.score; });
    if (entries.size() > 3)
        entries.resize(3);

    m_rankings[(int)mode] = entries;
}

void RankingManager::save(Mode mode)
{
    const QVector<Entry> &entries = m_rankings[(int)mode];
    QJsonArray arr;

    for (const Entry &e : entries) {
        QJsonObject obj;
        obj["score"]     = e.score;
        obj["photo"]     = e.photoPath;
        obj["photo2"]    = e.photoPath2;
        obj["timestamp"] = (double)e.timestamp;
        arr.append(obj);
    }

    QFile file(filePath(mode));
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
        file.close();
    }
}

QVector<RankingManager::Entry> RankingManager::getTop3(Mode mode) const
{
    return m_rankings.value((int)mode);
}

bool RankingManager::addScore(Mode mode, int score,
                              const QString &photo1, const QString &photo2)
{
    QVector<Entry> &entries = m_rankings[(int)mode];

    // 3위 안에 드는지 확인
    if (entries.size() >= 3 && score <= entries.last().score)
        return false;

    Entry e;
    e.score      = score;
    e.photoPath  = photo1;
    e.photoPath2 = photo2;
    e.timestamp  = QDateTime::currentSecsSinceEpoch();
    entries.append(e);

    // 내림차순 정렬, 상위 3개만 유지
    std::sort(entries.begin(), entries.end(),
              [](const Entry &a, const Entry &b) { return a.score > b.score; });
    if (entries.size() > 3)
        entries.resize(3);

    save(mode);
    return true;
}
