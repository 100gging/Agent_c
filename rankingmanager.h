#ifndef RANKINGMANAGER_H
#define RANKINGMANAGER_H

#include <QObject>
#include <QString>
#include <QVector>
#include <QMap>

class RankingManager : public QObject
{
    Q_OBJECT

public:
    enum Mode { Single, Competitive, Cooperative };

    struct Entry {
        int     score;
        QString photoPath;   // 플레이어 1 (또는 싱글) 사진 경로 — 카메라 추가 시 활용
        QString photoPath2;  // 플레이어 2 사진 경로 — 협동 모드용
        qint64  timestamp;
    };

    explicit RankingManager(const QString &dir = "ranking", QObject *parent = nullptr);

    /** 해당 모드의 상위 3개 항목 반환 (내림차순) */
    QVector<Entry> getTop3(Mode mode) const;

    /**
     * 점수를 랭킹에 추가 시도. 상위 3위 안에 들면 저장하고 true 반환.
     * photo1/photo2는 사진 경로 (현재는 빈 문자열).
     */
    bool addScore(Mode mode, int score,
                  const QString &photo1 = QString(),
                  const QString &photo2 = QString());

private:
    void load(Mode mode);
    void save(Mode mode);
    QString filePath(Mode mode) const;

    QString m_dir;
    QMap<int, QVector<Entry>> m_rankings;  // key: Mode as int
};

#endif // RANKINGMANAGER_H
