#ifndef ALSAPLAYER_H
#define ALSAPLAYER_H

#include <QObject>
#include <QThread>
#include <QMap>
#include <QString>
#include <QByteArray>
#include <QMutex>
#include <atomic>
#include <alsa/asoundlib.h>

// WAV 파일 정보
struct WavData {
    QByteArray pcmData;
    unsigned int sampleRate;
    unsigned int channels;
    unsigned int bitsPerSample;
};

// BGM 루프 재생용 스레드
class BgmThread : public QThread
{
    Q_OBJECT
public:
    BgmThread(QObject *parent = nullptr);
    void setWavData(const WavData &data);
    void stopPlayback();
    void setVolume(int vol); // 0~100

protected:
    void run() override;

private:
    WavData m_wav;
    std::atomic<bool> m_stop;
    std::atomic<int>  m_volume; // 0~100
};

// SFX 재생용 스레드 (1회 재생 후 자동 종료)
class SfxThread : public QThread
{
    Q_OBJECT
public:
    SfxThread(const WavData &data, int volume, QObject *parent = nullptr);

protected:
    void run() override;

private:
    WavData m_wav;
    int m_volume;
};

// ALSA 기반 오디오 플레이어
class AlsaPlayer : public QObject
{
    Q_OBJECT
public:
    explicit AlsaPlayer(QObject *parent = nullptr);
    ~AlsaPlayer();

    bool loadSfx(const QString &name, const QString &filePath);
    bool loadBgm(const QString &filePath);

    void playSfx(const QString &name);
    void playBgm();
    void stopBgm();

    void setSfxVolume(int vol); // 0~100
    void setBgmVolume(int vol); // 0~100

private:
    bool loadWav(const QString &filePath, WavData &out);

    QMap<QString, WavData> m_sfxMap;
    WavData m_bgmData;
    BgmThread *m_bgmThread;
    int m_sfxVolume;
    int m_bgmVolume;
};

#endif // ALSAPLAYER_H
