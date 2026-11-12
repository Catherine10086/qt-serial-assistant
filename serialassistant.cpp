#include "serialassistant.h"
#include "ui_serialassistant.h"
#include <QMessageBox>
#include <QDateTime>
#include <QRegularExpression>
#include <QTimer>

// 定义帧头静态成员（AABB）
const QByteArray SerialAssistant::FRAME_HEADER = QByteArray::fromHex("AABB");

// 在构造函数中打开日志文件
SerialAssistant::SerialAssistant(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::SerialAssistant)
    , totalSentBytes(0)
    , totalReceivedBytes(0)
    , totalPacketsSent(0)
    , totalPacketsReceived(0)
    , lostPackets(0)
    , logFile("packet_log.txt")
{
    ui->setupUi(this); // 设置UI
    serial = new QSerialPort(this); // 创建QSerialPort对象
    initSerialPort(); // 初始化串口设置

    connect(serial, &QSerialPort::readyRead, this, &SerialAssistant::readData); // 连接readyRead信号到readData槽
    connect(serial, &QSerialPort::errorOccurred, this, &SerialAssistant::handleError); // 连接errorOccurred信号到handleError槽

    // Windows 上拔线时 errorOccurred 不一定可靠触发，用定时器每秒轮询端口是否还在，作为兜底
    QTimer *aliveTimer = new QTimer(this);
    connect(aliveTimer, &QTimer::timeout, this, &SerialAssistant::checkPortAlive);
    aliveTimer->start(1000);

    // 添加状态标签到状态栏
    statusBar()->addPermanentWidget(ui->packetLossLabel);
    statusBar()->addPermanentWidget(ui->totalSentLabel);
    statusBar()->addPermanentWidget(ui->totalReceivedLabel);

    updateStatusBar(); // 更新状态栏

    // 打开日志文件进行写入
    if (!logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        QMessageBox::critical(this, tr("错误"), tr("无法打开日志文件!"));
    } else {
        logStream.setDevice(&logFile); // 设置日志流的设备为日志文件
    }
}

// 析构函数，关闭日志文件
SerialAssistant::~SerialAssistant()
{
    delete ui;
    logFile.close();
}

// 初始化串口设置
void SerialAssistant::initSerialPort()
{
    updatePortList(); // 更新可用的串口列表

    // 添加波特率选项
    ui->baudRateCombo->addItem(QStringLiteral("115200"), QSerialPort::Baud115200);
    ui->baudRateCombo->addItem(QStringLiteral("9600"), QSerialPort::Baud9600);


    // 添加数据位选项
    ui->dataBitsCombo->addItem(QStringLiteral("8"), QSerialPort::Data8);
    ui->dataBitsCombo->addItem(QStringLiteral("7"), QSerialPort::Data7);

    // 添加校验位选项
    ui->parityCombo->addItem(QStringLiteral("None"), QSerialPort::NoParity);
    ui->parityCombo->addItem(QStringLiteral("Even"), QSerialPort::EvenParity);
    ui->parityCombo->addItem(QStringLiteral("Odd"), QSerialPort::OddParity);

    // 添加停止位选项
    ui->stopBitsCombo->addItem(QStringLiteral("1"), QSerialPort::OneStop);
    ui->stopBitsCombo->addItem(QStringLiteral("2"), QSerialPort::TwoStop);
}

// 更新串口列表
void SerialAssistant::updatePortList()
{
    ui->portCombo->clear(); // 清空串口下拉列表
    const auto infos = QSerialPortInfo::availablePorts(); // 获取可用串口信息
    for (const QSerialPortInfo &info : infos) {
        ui->portCombo->addItem(info.portName()); // 将串口名称添加到下拉列表中
    }
}

// 打开/关闭串口按钮点击事件处理函数
void SerialAssistant::on_openCloseButton_clicked()
{
    if (serial->isOpen()) {
        serial->close(); // 关闭串口
        ui->openCloseButton->setText("打开"); // 更新按钮文本
        statusBar()->showMessage("串口已关闭"); // 更新状态栏信息
    } else {
        // 设置串口参数
        serial->setPortName(ui->portCombo->currentText());
        serial->setBaudRate(ui->baudRateCombo->currentData().toInt());
        serial->setDataBits(static_cast<QSerialPort::DataBits>(ui->dataBitsCombo->currentData().toInt()));
        serial->setParity(static_cast<QSerialPort::Parity>(ui->parityCombo->currentData().toInt()));
        serial->setStopBits(static_cast<QSerialPort::StopBits>(ui->stopBitsCombo->currentData().toInt()));

        if (serial->open(QIODevice::ReadWrite)) {
            ui->openCloseButton->setText("关闭"); // 更新按钮文本
            statusBar()->showMessage("串口已打开"); // 更新状态栏信息
        } else {
            QMessageBox::critical(this, tr("错误"), serial->errorString()); // 弹出错误信息
            statusBar()->showMessage("打开串口失败"); // 更新状态栏信息
        }
    }
}

// 发送按钮点击事件处理函数
void SerialAssistant::on_sendButton_clicked()
{
    if (serial->isOpen()) {
        QByteArray data;
        if (ui->hexSendCheckBox->isChecked()) {
            // 十六进制发送：把输入当作 hex 字符串解析成原始字节
            QString hexText = ui->sendTextEdit->toPlainText();
            hexText.remove(QRegularExpression("\\s")); // 去掉所有空白字符
            if (hexText.isEmpty() || hexText.size() % 2 != 0 ||
                !QRegularExpression("^[0-9A-Fa-f]+$").match(hexText).hasMatch()) {
                QMessageBox::warning(this, tr("警告"),
                                     tr("十六进制格式错误！请输入合法的十六进制字符串，例如 AABB 或 31 12。"));
                return;
            }
            data = QByteArray::fromHex(hexText.toLatin1()); // 转成原始字节
        } else {
            data = ui->sendTextEdit->toPlainText().toUtf8(); // 文本发送
        }

        // 按长度字段协议组帧：帧头 + 数据长度(2字节大端) + 数据体 + CRC
        QByteArray packet = FRAME_HEADER;
        packet.append((char)(data.size() >> 8));   // 长度字段高字节
        packet.append((char)(data.size() & 0xFF)); // 长度字段低字节
        packet.append(data);                        // 数据体
        quint16 crc = calculateCRC(packet); // 计算CRC校验码（覆盖帧头+长度+数据体）
        packet.append((char)(crc >> 8)); // 添加高位CRC
        packet.append((char)(crc & 0xFF)); // 添加低位CRC

        qint64 bytesWritten = serial->write(packet); // 发送数据包
        if (bytesWritten == -1) {
            QMessageBox::warning(this, tr("发送错误"), tr("数据发送失败!")); // 弹出警告信息
        } else {
            totalSentBytes += bytesWritten; // 更新发送字节数
            totalPacketsSent++; // 更新发送包数
            updateStatusBar(); // 更新状态栏
        }
    } else {
        QMessageBox::warning(this, tr("警告"), tr("串口未打开!")); // 弹出警告信息
    }
}

// 读取数据
void SerialAssistant::readData()
{
    // 此处使用接收缓冲区解决高频数据下的粘包/拆包问题：
    // 串口是字节流，一帧可能被拆成多次 readyRead，也可能多帧粘在一起一次到达，
    // 所以先用 m_buffer 累积数据，再按长度字段协议拆出完整帧。
    m_buffer.append(serial->readAll()); // 读取所有可用数据追加到接收缓冲区

    while (true) {
        // 至少需要帧头+长度字段才能判断一帧的长度
        if (m_buffer.size() < FRAME_HEADER_SIZE + LENGTH_FIELD_SIZE) {
            break;
        }

        // 帧头不对齐：丢弃一个字节重新同步，直到找到帧头
        if (!m_buffer.startsWith(FRAME_HEADER)) {
            m_buffer.remove(0, 1);
            continue;
        }

        // 读取长度字段（2字节大端），表示数据体字节数
        quint16 bodyLen = ((quint8)m_buffer[FRAME_HEADER_SIZE] << 8) |
                          (quint8)m_buffer[FRAME_HEADER_SIZE + 1];

        // 长度字段异常保护：防止坏长度导致缓冲区无限等待
        if (bodyLen > MAX_FRAME_SIZE) {
            m_buffer.remove(0, 1);
            continue;
        }

        int frameSize = FRAME_HEADER_SIZE + LENGTH_FIELD_SIZE + bodyLen + CRC_SIZE;

        // 帧还没收全，等待下一次 readyRead
        if (m_buffer.size() < frameSize) {
            break;
        }

        QByteArray packet = m_buffer.left(frameSize); // 取出完整一帧
        m_buffer.remove(0, frameSize);                // 从缓冲区移除该帧

        totalReceivedBytes += packet.size(); // 更新接收字节数
        totalPacketsReceived++; // 更新接收包数

        if (verifyCRC(packet)) {
            QByteArray data = packet.mid(FRAME_HEADER_SIZE + LENGTH_FIELD_SIZE, bodyLen); // 提取数据体
            if (ui->hexDisplayCheckBox->isChecked()) {
                // 十六进制显示：把字节转成带空格的 hex 字符串
                ui->receiveTextEdit->append(QString::fromLatin1(data.toHex(' ').toUpper()));
            } else {
                ui->receiveTextEdit->append(QString::fromUtf8(data)); // 文本显示
            }
        } else {
            ui->receiveTextEdit->append("接收到损坏的数据!"); // 显示接收到的损坏数据
            lostPackets++; // 更新丢包数
            logLostPacket(packet); // 记录丢包信息
        }
    }

    updateStatusBar(); // 更新状态栏
}

// 处理串口错误（如设备掉线）
void SerialAssistant::handleError(QSerialPort::SerialPortError error)
{
    // ResourceError 表示设备被意外移除（如 USB 掉线）
    if (error == QSerialPort::ResourceError) {
        handleDeviceDisconnected();
    }
}

// 定时检查当前打开的串口是否还存在于系统中（Windows 上拔线的兜底检测）
void SerialAssistant::checkPortAlive()
{
    if (!serial->isOpen()) {
        return; // 串口没打开，无需检查
    }

    const auto infos = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &info : infos) {
        if (info.portName() == serial->portName()) {
            return; // 串口还在，正常
        }
    }

    handleDeviceDisconnected(); // 串口已从系统消失，视为掉线
}

// 统一的设备掉线处理：关串口、清缓冲、复位界面、提示用户
void SerialAssistant::handleDeviceDisconnected()
{
    if (serial->isOpen()) {
        serial->close(); // 关闭串口
    }
    m_buffer.clear(); // 清空接收缓冲区，避免残留半帧在重连后造成误判
    ui->openCloseButton->setText("打开"); // 复位按钮状态
    statusBar()->showMessage("设备已断开"); // 更新状态栏信息
    QMessageBox::critical(this, tr("串口错误"), tr("设备已断开连接，串口已关闭！请重新插入设备。"));
    updatePortList(); // 刷新端口列表（重插后端口号可能变化）
}

// 清空接收窗口按钮点击事件处理函数
void SerialAssistant::on_clearReceiveButton_clicked()
{
    ui->receiveTextEdit->clear(); // 清空接收窗口
}

// 清空发送窗口按钮点击事件处理函数
void SerialAssistant::on_clearSendButton_clicked()
{
    ui->sendTextEdit->clear(); // 清空发送窗口
}

// 计算CRC校验码
quint16 SerialAssistant::calculateCRC(const QByteArray &data)
{
    quint16 crc = 0xFFFF; // 初始化CRC
    for (int i = 0; i < data.size(); i++) {
        crc ^= (quint8)data[i]; // 逐字节计算CRC
        for (int j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001; // 如果最低位为1，则右移并异或多项式
            } else {
                crc = crc >> 1; // 否则直接右移
            }
        }
    }
    return crc;
}

// 校验CRC
bool SerialAssistant::verifyCRC(const QByteArray &packet)
{
    if (packet.size() < 2) return false; // 如果数据包长度小于2，则返回false
    QByteArray data = packet.left(packet.size() - 2); // 获取数据部分，不包括CRC
    quint16 receivedCRC = ((quint8)packet[packet.size() - 2] << 8) | (quint8)packet[packet.size() - 1]; // 提取收到的CRC
    quint16 calculatedCRC = calculateCRC(data); // 计算数据部分的CRC
    return receivedCRC == calculatedCRC; // 返回校验结果
}

// 更新状态栏
void SerialAssistant::updateStatusBar()
{
    ui->packetLossLabel->setText(QString("丢包个数: %1").arg(lostPackets)); // 更新丢包个数标签
    ui->totalSentLabel->setText(QString("发送总计: %1 字节").arg(totalSentBytes)); // 更新发送字节数标签
    ui->totalReceivedLabel->setText(QString("接收总计: %1 字节").arg(totalReceivedBytes)); // 更新接收字节数标签
}

// 记录丢包信息
void SerialAssistant::logLostPacket(const QByteArray &packet)
{
    if (logStream.device() != nullptr) {
        QString packetString = QString::fromUtf8(packet); // 将包数据转换为字符串
        logStream << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss")
                  << " - 丢包: " << packet.toHex() << " \n(字符串: " << packetString << ")\n"; // 写入日志
        logStream.flush(); // 刷新日志流
    }
}

