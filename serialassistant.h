#ifndef SERIALASSISTANT_H
#define SERIALASSISTANT_H

#include <QMainWindow>
#include <QtSerialPort/QSerialPort>
#include <QtSerialPort/QSerialPortInfo>
#include <QByteArray>
#include <QFile>
#include <QTextStream>

QT_BEGIN_NAMESPACE
namespace Ui { class SerialAssistant; }
QT_END_NAMESPACE

class SerialAssistant : public QMainWindow
{
    Q_OBJECT

public:
    SerialAssistant(QWidget *parent = nullptr);
    ~SerialAssistant();

private slots:
    void on_openCloseButton_clicked();
    void on_sendButton_clicked();
    void readData();
    void handleError(QSerialPort::SerialPortError error);
    void checkPortAlive();
    void on_clearReceiveButton_clicked();
    void on_clearSendButton_clicked();

private:
    Ui::SerialAssistant *ui;
    QSerialPort *serial;
    void initSerialPort();
    void updatePortList();
    quint16 calculateCRC(const QByteArray &data);
    bool verifyCRC(const QByteArray &packet);
    void updateStatusBar();
    void logLostPacket(const QByteArray &packet);
    void handleDeviceDisconnected();

    qint64 totalSentBytes;
    qint64 totalReceivedBytes;
    int totalPacketsSent;
    int totalPacketsReceived;
    int lostPackets;

    QByteArray m_buffer; // 接收缓冲区：累积数据，配合长度字段协议解决粘包/拆包

    // 帧格式：帧头(2字节) + 数据长度(2字节 大端) + 数据体(N字节) + CRC(2字节)
    static const QByteArray FRAME_HEADER;
    static constexpr int FRAME_HEADER_SIZE = 2;
    static constexpr int LENGTH_FIELD_SIZE = 2;
    static constexpr int CRC_SIZE = 2;
    static constexpr int MAX_FRAME_SIZE = 4096; // 数据体最大长度，防止坏长度导致缓冲区无限等待
    QFile logFile;
    QTextStream logStream;
};

#endif // SERIALASSISTANT_H
