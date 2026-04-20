#include <QCoreApplication>
#include <QProcess>
#include <QProcessEnvironment>
#include <QDebug>

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);

    QProcess process;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

    // alsa.sh 내용 반영
    env.insert("ALSA_CONFIG_PATH", "/mnt/nfs/alsa-lib/share/alsa/alsa.conf");

    QString ld = env.value("LD_LIBRARY_PATH");
    if (!ld.isEmpty())
        ld += ":";

    ld += "/mnt/nfs/alsa-lib/lib";
    ld += ":/mnt/nfs/alsa-lib/lib/alsa-lib";
    ld += ":/mnt/nfs/alsa-lib/lib/alsa-lib/smixer";

    env.insert("LD_LIBRARY_PATH", ld);

    process.setProcessEnvironment(env);

    // aplay 실행
    process.start("/mnt/nfs/aplay",
                  QStringList() << "-Dhw:0,0"
                                << "/mnt/nfs/test_contents/test_digimon.wav");

    if (!process.waitForStarted()) {
        qDebug() << "aplay start failed";
        return -1;
    }

    process.waitForFinished(-1);

    qDebug() << "stdout:" << process.readAllStandardOutput();
    qDebug() << "stderr:" << process.readAllStandardError();

    return 0;
}