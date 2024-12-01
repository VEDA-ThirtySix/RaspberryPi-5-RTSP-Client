#include "streaming.h"

#include <QTimer>
#include <QDateTime>
#include <QMessageBox>

Streaming::Streaming(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Streaming)
{
    ui->setupUi(this);

    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &Streaming::updateDateTime);
    timer->start(1000);

    streamSocket = new QTcpSocket(this);

    connect(streamSocket, &QTcpSocket::connected, this, &Streaming::onConnected);
    connect(streamSocket, &QTcpSocket::disconnected, this, &Streaming::onDisconnected);
    connect(streamSocket, &QTcpSocket::readyRead, this, &Streaming::readStream);
    connect(streamSocket, &QAbstractSocket::errorOccurred, this, &Streaming::handleError);



    ui->streamingLabel->setMinimumSize(FRAME_WIDTH, FRAME_HEIGHT);
    ui->streamingLabel->setAlignment(Qt::AlignCenter);

    setupStreamingConnection();
    updateDateTime();// 초기 날짜/시간 표시
}

Streaming::~Streaming()
{
    if (streamSocket->state() == QAbstractSocket::ConnectedState) {
        streamSocket->disconnectFromHost();
    }
    delete ui;
}

void Streaming::setupStreamingConnection()
{
    const QString serverIP = "192.168.0.40";
    const quint16 serverPort = 5100;

    streamSocket->connectToHost(serverIP, serverPort);
}

void Streaming::onConnected()
{
    qDebug() << "Connected to streaming server";
}

void Streaming::onDisconnected()
{
    qDebug() << "Disconnected from streaming server";
    QTimer::singleShot(1000, this, &Streaming::reconnectToStream);
}

void Streaming::readStream()
{
    frameBuffer.append(streamSocket->readAll());

    // 프레임을 수신했는지 확인
    while (frameBuffer.size() >= FRAME_SIZE) {
        QByteArray frameData = frameBuffer.left(FRAME_SIZE);
        frameBuffer.remove(0, FRAME_SIZE);

        processYUYVFrame(frameData);
    }
}

void Streaming::processYUYVFrame(const QByteArray &frameData)
{
    // YUYV 데이터를 RGB로 변환
    QImage image(FRAME_WIDTH, FRAME_HEIGHT, QImage::Format_RGB888);
    const uchar *data = reinterpret_cast<const uchar*>(frameData.constData());

    for (int y = 0; y < FRAME_HEIGHT; y++) {
        for (int x = 0; x < FRAME_WIDTH; x += 2) {
            int i = (y * FRAME_WIDTH + x) * 2;
            int y0 = data[i];
            int u = data[i + 1];
            int y1 = data[i + 2];
            int v = data[i + 3];

            // YUYV를 RGB로 변환하는 공식
            int r0 = qBound(0, static_cast<int>(y0 + 1.402 * (v - 128)), 255);
            int g0 = qBound(0, static_cast<int>(y0 - 0.344136 * (u - 128) - 0.714136 * (v - 128)), 255);
            int b0 = qBound(0, static_cast<int>(y0 + 1.772 * (u - 128)), 255);

            int r1 = qBound(0, static_cast<int>(y1 + 1.402 * (v - 128)), 255);
            int g1 = qBound(0, static_cast<int>(y1 - 0.344136 * (u - 128) - 0.714136 * (v - 128)), 255);
            int b1 = qBound(0, static_cast<int>(y1 + 1.772 * (u - 128)), 255);

            image.setPixelColor(x, y, QColor(r0, g0, b0));
            image.setPixelColor(x + 1, y, QColor(r1, g1, b1));
        }
    }

    ui->streamingLabel->setPixmap(QPixmap::fromImage(image));
}

void Streaming::handleError(QAbstractSocket::SocketError socketError)
{
    switch (socketError) {
    case QAbstractSocket::RemoteHostClosedError:
        qDebug() << "Connection closed by server";
        break;
    case QAbstractSocket::HostNotFoundError:
        if (!errorMessageShown) {
            qDebug() << "The host was not found";
            QMessageBox::warning(this, tr("Streaming Client"), tr("The host was not found."));
            errorMessageShown = true;
        }
        break;
    case QAbstractSocket::ConnectionRefusedError:
        if (!errorMessageShown) {
            qDebug() << "The connection was refused";
            QMessageBox::warning(this, tr("Streaming Client"), tr("The connection was refused."));
            errorMessageShown = true;
        }
        break;
    default:
        break;
    }

    QTimer::singleShot(500, this, &Streaming::reconnectToStream);
}

void Streaming::reconnectToStream()
{
    if (streamSocket->state() == QAbstractSocket::UnconnectedState) {
        setupStreamingConnection();
    }
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

