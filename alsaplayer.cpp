#include "alsaplayer.h"
#include <QFile>
#include <QDebug>
#include <cstring>

// ─── BgmThread ────────────────────────────────────────────────────
BgmThread::BgmThread(QObject *parent)
    : QThread(parent), m_stop(false), m_volume(70)
{
}

void BgmThread::setWavData(const WavData &data)
{
    m_wav = data;
}

void BgmThread::stopPlayback()
{
    m_stop = true;
}

void BgmThread::setVolume(int vol)
{
    m_volume = qBound(0, vol, 100);
}

void BgmThread::run()
{
    m_stop = false;

    snd_pcm_t *pcm = nullptr;
    int err = snd_pcm_open(&pcm, "default", SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        qWarning() << "BGM: ALSA open error:" << snd_strerror(err);
        return;
    }

    snd_pcm_format_t fmt = (m_wav.bitsPerSample == 16)
        ? SND_PCM_FORMAT_S16_LE : SND_PCM_FORMAT_U8;

    snd_pcm_set_params(pcm, fmt, SND_PCM_ACCESS_RW_INTERLEAVED,
                       m_wav.channels, m_wav.sampleRate, 1, 100000);

    int frameSize = m_wav.channels * (m_wav.bitsPerSample / 8);
    int totalFrames = m_wav.pcmData.size() / frameSize;
    const int chunkFrames = 1024;

    // 볼륨을 적용한 버퍼
    QByteArray buf(chunkFrames * frameSize, 0);

    while (!m_stop) {
        int pos = 0;
        while (pos < totalFrames && !m_stop) {
            int frames = qMin(chunkFrames, totalFrames - pos);
            int bytes = frames * frameSize;

            // 볼륨 적용
            const char *src = m_wav.pcmData.constData() + pos * frameSize;
            if (m_wav.bitsPerSample == 16) {
                const int16_t *in = reinterpret_cast<const int16_t *>(src);
                int16_t *out = reinterpret_cast<int16_t *>(buf.data());
                int samples = bytes / 2;
                int vol = m_volume.load();
                for (int i = 0; i < samples; i++) {
                    out[i] = static_cast<int16_t>((in[i] * vol) / 100);
                }
            } else {
                memcpy(buf.data(), src, bytes);
            }

            snd_pcm_sframes_t written = snd_pcm_writei(pcm, buf.constData(), frames);
            if (written < 0) {
                snd_pcm_recover(pcm, written, 0);
            }
            pos += frames;
        }
        // 루프: 처음부터 다시
    }

    snd_pcm_drain(pcm);
    snd_pcm_close(pcm);
}

// ─── SfxThread ────────────────────────────────────────────────────
SfxThread::SfxThread(const WavData &data, int volume, QObject *parent)
    : QThread(parent), m_wav(data), m_volume(qBound(0, volume, 100))
{
    connect(this, &QThread::finished, this, &QObject::deleteLater);
}

void SfxThread::run()
{
    snd_pcm_t *pcm = nullptr;
    int err = snd_pcm_open(&pcm, "default", SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        qWarning() << "SFX: ALSA open error:" << snd_strerror(err);
        return;
    }

    snd_pcm_format_t fmt = (m_wav.bitsPerSample == 16)
        ? SND_PCM_FORMAT_S16_LE : SND_PCM_FORMAT_U8;

    snd_pcm_set_params(pcm, fmt, SND_PCM_ACCESS_RW_INTERLEAVED,
                       m_wav.channels, m_wav.sampleRate, 1, 50000);

    int frameSize = m_wav.channels * (m_wav.bitsPerSample / 8);
    int totalFrames = m_wav.pcmData.size() / frameSize;
    const int chunkFrames = 1024;
    QByteArray buf(chunkFrames * frameSize, 0);

    int pos = 0;
    while (pos < totalFrames) {
        int frames = qMin(chunkFrames, totalFrames - pos);
        int bytes = frames * frameSize;

        const char *src = m_wav.pcmData.constData() + pos * frameSize;
        if (m_wav.bitsPerSample == 16) {
            const int16_t *in = reinterpret_cast<const int16_t *>(src);
            int16_t *out = reinterpret_cast<int16_t *>(buf.data());
            int samples = bytes / 2;
            for (int i = 0; i < samples; i++) {
                out[i] = static_cast<int16_t>((in[i] * m_volume) / 100);
            }
        } else {
            memcpy(buf.data(), src, bytes);
        }

        snd_pcm_sframes_t written = snd_pcm_writei(pcm, buf.constData(), frames);
        if (written < 0) {
            snd_pcm_recover(pcm, written, 0);
        }
        pos += frames;
    }

    snd_pcm_drain(pcm);
    snd_pcm_close(pcm);
}

// ─── AlsaPlayer ──────────────────────────────────────────────────
AlsaPlayer::AlsaPlayer(QObject *parent)
    : QObject(parent), m_bgmThread(nullptr), m_sfxVolume(100), m_bgmVolume(70)
{
}

AlsaPlayer::~AlsaPlayer()
{
    stopBgm();
}

bool AlsaPlayer::loadWav(const QString &filePath, WavData &out)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "WAV open failed:" << filePath;
        return false;
    }

    QByteArray all = file.readAll();
    file.close();

    if (all.size() < 44) {
        qWarning() << "WAV too small:" << filePath;
        return false;
    }

    const char *d = all.constData();

    // RIFF 헤더 검증
    if (memcmp(d, "RIFF", 4) != 0 || memcmp(d + 8, "WAVE", 4) != 0) {
        qWarning() << "Not a WAV file:" << filePath;
        return false;
    }

    // fmt 청크 찾기
    int pos = 12;
    unsigned int channels = 0, sampleRate = 0, bitsPerSample = 0;
    int dataOffset = 0, dataSize = 0;

    while (pos + 8 <= all.size()) {
        char chunkId[5] = {0};
        memcpy(chunkId, d + pos, 4);
        uint32_t chunkSize = 0;
        memcpy(&chunkSize, d + pos + 4, 4);

        if (memcmp(chunkId, "fmt ", 4) == 0) {
            if (pos + 8 + 16 <= all.size()) {
                uint16_t audioFmt;
                memcpy(&audioFmt, d + pos + 8, 2);
                uint16_t ch;
                memcpy(&ch, d + pos + 10, 2);
                channels = ch;
                memcpy(&sampleRate, d + pos + 12, 4);
                uint16_t bps;
                memcpy(&bps, d + pos + 22, 2);
                bitsPerSample = bps;
            }
        } else if (memcmp(chunkId, "data", 4) == 0) {
            dataOffset = pos + 8;
            dataSize = chunkSize;
            break;
        }
        pos += 8 + chunkSize;
    }

    if (dataOffset == 0 || channels == 0 || sampleRate == 0 || bitsPerSample == 0) {
        qWarning() << "WAV parse error:" << filePath;
        return false;
    }

    out.pcmData = all.mid(dataOffset, dataSize);
    out.channels = channels;
    out.sampleRate = sampleRate;
    out.bitsPerSample = bitsPerSample;

    qDebug() << "Loaded WAV:" << filePath
             << channels << "ch" << sampleRate << "Hz" << bitsPerSample << "bit"
             << dataSize << "bytes";
    return true;
}

bool AlsaPlayer::loadSfx(const QString &name, const QString &filePath)
{
    WavData wav;
    if (!loadWav(filePath, wav))
        return false;
    m_sfxMap[name] = wav;
    return true;
}

bool AlsaPlayer::loadBgm(const QString &filePath)
{
    return loadWav(filePath, m_bgmData);
}

void AlsaPlayer::playSfx(const QString &name)
{
    if (!m_sfxMap.contains(name)) return;
    // 새 스레드에서 재생 (완료 후 자동 삭제)
    SfxThread *t = new SfxThread(m_sfxMap[name], m_sfxVolume);
    t->start();
}

void AlsaPlayer::playBgm()
{
    if (m_bgmData.pcmData.isEmpty()) return;
    stopBgm();
    m_bgmThread = new BgmThread(this);
    m_bgmThread->setWavData(m_bgmData);
    m_bgmThread->setVolume(m_bgmVolume);
    m_bgmThread->start();
}

void AlsaPlayer::stopBgm()
{
    if (m_bgmThread && m_bgmThread->isRunning()) {
        m_bgmThread->stopPlayback();
        m_bgmThread->wait(2000);
        delete m_bgmThread;
        m_bgmThread = nullptr;
    }
}

void AlsaPlayer::setSfxVolume(int vol)
{
    m_sfxVolume = qBound(0, vol, 100);
}

void AlsaPlayer::setBgmVolume(int vol)
{
    m_bgmVolume = qBound(0, vol, 100);
    if (m_bgmThread)
        m_bgmThread->setVolume(m_bgmVolume);
}
