#include "streaming.h"
#include "ui_streaming.h"
#include <QDebug>
#include <QPixmap>
#include <QImage>
#include <QLayout>

#include <QTimer>
#include <QDateTime>
#include <QMessageBox>

#define protocol "rtsp"
#define port 8554

Streaming::Streaming(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Streaming)
    , timer(new QTimer(this))
    , ffmpegProcess(new QProcess(this))
    , frameTimer(new QTimer(this))
    , m_rtspUrl("") // 초기 URL 비우기
{
    ui->setupUi(this);

    // 타이머로 날짜/시간 업데이트
    connect(timer, &QTimer::timeout, this, &Streaming::updateDateTime);
    timer->start(1000); //1초마다 실행
    updateDateTime();// 초기 날짜/시간 표시

    connect(frameTimer, &QTimer::timeout, this, &Streaming::captureFrame);
    //connect(ffmpegProcess, SIGNAL(readyReadStandardOutput()), this, SLOT(captureFrame()));


}

Streaming::~Streaming()
{
    if (ffmpegProcess->state() == QProcess::Running) {
        ffmpegProcess->terminate();
        if (!ffmpegProcess->waitForFinished(3000)) { // 3초 대기
                        ffmpegProcess->kill(); // 종료 실패 시 강제 종료
                    }
    }
    delete ui;
    //delete mainWindow;
}

void Streaming::updateDateTime()
{
    QDateTime current = QDateTime::currentDateTime();

    // 요일 한글 변환
    QMap<int, QString> weekdays;
    weekdays[1] = "MON";
    weekdays[2] = "TUE";
    weekdays[3] = "WED";
    weekdays[4] = "THU";
    weekdays[5] = "FRI";
    weekdays[6] = "SAT";
    weekdays[7] = "SUN";

    QString weekday = weekdays[current.date().dayOfWeek()];

    // 오전/오후 판단
    QString ampm = current.time().hour() < 12 ? "AM" : "PM";

    // 12시간제로 변환
    int hour = current.time().hour() % 12;
    if (hour == 0) hour = 12;

    // 날짜/시간 문자열 생성
    QString dateTimeStr = QString::asprintf("%04d.%02d.%02d %s | %s %02d:%02d:%02d",
                            current.date().year(),
                            current.date().month(),
                            current.date().day(),
                            qPrintable(weekday),
                            qPrintable(ampm),
                            hour,
                            current.time().minute(),
                            current.time().second());

    ui->dateTimeLabel->setText(dateTimeStr);
}


/*RTSP스트림을 가져오고, 파이프로 출력*/
void Streaming::startFFmpeg() {

    if (ffmpegProcess->state() == QProcess::Running) {
        qDebug() << "FFmpeg is already running!";
        return;
    }

    if (m_rtspUrl.isEmpty()) {
        //ui->messageInput->text().isEmpty()
        QMessageBox::warning(this, "Error", "RTSP URL is not set. Please set the address first.");
        return;
    }

#ifdef Q_OS_MAC
    QString ffmpegPath = "/opt/homebrew/bin/ffmpeg"; // macOS의 FFmpeg 경로
#elif defined(Q_OS_LINUX)
    QString ffmpegPath = "/usr/bin/ffmpeg"; // Linux의 일반적인 FFmpeg 경로
#endif

    //FFMPEG 실행시 전달되는 옵션
    QStringList arguments = {
        "-protocol_whitelist","file,tcp,udp,rtp,rtsp",
        "-i",m_rtspUrl, //동적으로 설정된 RTSP URL
        "-vf", "fps=30",                    // 출력 프레임 속도 설정
        "-s", "800x600",                         // 출력 영상의 해상도 설정
        "-f","rawvideo",
        "-pix_fmt","rgb24",  // 픽셀 포맷: RGB888
        //"-pix_fmt", "yuvj420p",  // 색 공간 최적화
        "-loglevel","debug",
        "-"
    };


    ffmpegProcess->start(ffmpegPath, arguments);

    if (!ffmpegProcess->waitForStarted()) {
        qDebug() << "FFmpeg failed to start!";
        return;
    }
    frameTimer->start(33); // 30fps (1000ms /33 = 30ms)
    qDebug() << "FFmpeg started with URL:" << m_rtspUrl;

    setButtonStyle(ui->startButton, true);
    setButtonStyle(ui->stopButton, false);

}

void Streaming::captureFrame() {
    while (ffmpegProcess->bytesAvailable() > 0) {
        // FFmpeg에서 데이터 읽기
        QByteArray frameData = ffmpegProcess->readAllStandardOutput();
        if (frameData.isEmpty()) {
            qDebug() << "No frame data received!";
            return;
        }
        //qDebug() << "frameData Size : " << frameData.size();
        // 이전에 누적된 데이터와 합치기
        incompleteBuffer.append(frameData);

        // 한 프레임 크기 계산 (RGB888)
        const int width = 800;
        const int height = 600;
        const int bytesPerPixel = 3;
        const int frameSize = width * height * bytesPerPixel;

        // 버퍼가 프레임 크기 이상인지 확인
        while (incompleteBuffer.size() >= frameSize) {
            QByteArray singleFrameData = incompleteBuffer.left(frameSize);
            incompleteBuffer.remove(0, frameSize);

            // QImage 생성
            QImage image(reinterpret_cast<const uchar*>(singleFrameData.data()),
                         width, height, QImage::Format_RGB888);

            // QLabel에 맞게 크기 조정
            QPixmap scaledPixmap = QPixmap::fromImage(image).scaled(
                ui->streamingLabel->size(),
                Qt::KeepAspectRatioByExpanding,
                Qt::SmoothTransformation
            );

            // QLabel에 QPixmap 설정
            ui->streamingLabel->setPixmap(scaledPixmap);
        }
    }
}

/*FFmpeg을 종료하고 마지막 프레임을 고정*/
void Streaming::stopFFmpeg() {
    if (ffmpegProcess->state() == QProcess::Running) {
        frameTimer->stop(); // 프레임 캡처 중지
        ffmpegProcess->terminate(); // 프로세스 종료 요청
        if (!ffmpegProcess->waitForFinished(3000)) { // 3초 대기
            ffmpegProcess->kill(); // 강제 종료
        }
        qDebug() << "FFmpeg process stopped.";
    } else {
        qDebug() << "FFmpeg is not running.";
    }

    setButtonStyle(ui->startButton, false);
    setButtonStyle(ui->stopButton, true);
}

/*FFmpeg 프로세스의 에러로그를 읽어서 출력*/
void Streaming::processOutput() {
    QByteArray errorOutput = ffmpegProcess->readAllStandardError();
    if (!errorOutput.isEmpty()) {
            qDebug() << "FFmpeg Error:" << errorOutput;
        }
}


/*UI에서 입력된 RTSP주소 가져오기*/
void Streaming::get_host(const QString& host) {
    m_host = host;
}

void Streaming::rtsp_setting(){

    // RTSP URL(QString) 생성
    m_rtspUrl = QString("%1://%2:%3/")
                    .arg(protocol)
                    .arg(m_host)
                    .arg(port);


    ui->label_url->setText(m_rtspUrl);
    qDebug() << "QDEBUG(SW)$ RTSP URL:" << m_rtspUrl;
}

void Streaming::on_startButton_clicked(){
    startFFmpeg();
}

void Streaming::on_stopButton_clicked(){
    stopFFmpeg();
}

//버튼 true, false에 따른 스타일 지정 함수
void Streaming::setButtonStyle(QPushButton* button, bool isActive) {
    if (isActive) {
        button->setChecked(true);
        button->setStyleSheet("color: white; \
                              background-color:rgba(243,115,33,0.7); \
                              border:1px solid rgb(243,115,33); \
                              font: 500 9pt 'Quicksand Medium';");
    } else {
        button->setChecked(false);
        button->setStyleSheet("color: rgb(243,115,33); \
                              background:none; \
                              border:1px solid rgb(243,115,33); \
                              font: 500 9pt 'Quicksand Medium';");
    }
}


