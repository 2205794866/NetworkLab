#include "workthread.h"

#include <utility>
std::string getCurrentTime()
{
    time_t nowtime;
    struct tm t;
    nowtime = time(NULL);
    // localtime_s(&t, &nowtime);
    t = *localtime(&nowtime);
    char tmp[64];
    strftime(tmp,sizeof(tmp),"%Y-%m-%d %H:%M:%S",&t);
    return tmp;
}

/// @brief 构造函数
/// @param type 记录类型 0用户操作 1传输成功 2传输失败 3超时重传
/// @param status 记录状态 0成功 1失败
/// @param msg 记录信息 传输文件名或者错误信息
Record::Record(int type,int status, std::string msg)
{
    const char *recordType[] = {"用户操作", "传输成功", "传输失败", "重传"};
    const char *recordStatus[] = {"成功", "失败"};
    this->time = getCurrentTime();
    this->type = recordType[type];
    this->status = recordStatus[status];
    this->msg = std::move(msg);
}

/// @brief 构造函数2
/// @param str 模板字符串
Record::Record(std::string str)
{
    fromString(std::move(str));
}

Record::~Record()
= default;

std::string Record::toString()
{
    return this->time + " "  + this->type + " " + this->status + " " + this->msg;
}

void Record::fromString(std::string str)
{
    int pos = str.find(" ");
    this->time = str.substr(0, pos);
    str = str.substr(pos + 1);
    pos = str.find(" ");
    this->status = str.substr(0, pos);
    str = str.substr(pos + 1);
    pos = str.find(" ");
    this->type = str.substr(0, pos);
    str = str.substr(pos + 1);
    this->msg = str;
}


workThread::workThread()
{
    strcpy(serverIP, "127.0.0.1");
    port = TFTP_PORT;
    strcpy(fileName, "test.txt");
    op = TFTP_OPCODE_RRQ;
    mode = TFTP_OCTET;
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
}

workThread::~workThread()
{
    WSACleanup();
}



void workThread::clear(FILE *fp, SOCKET sock) {
    fclose(fp);
    closesocket(sock);
}

void workThread::startV()
{
    startTime = chrono::high_resolution_clock::now();
    timePoint = startTime;
}

void workThread::stopV()
{
    endTime = chrono::high_resolution_clock::now();
}

/// @brief 输出瞬时速率
/// @param bytes 
void workThread::showInsV(int bytes)
{
    // 获取当前时间
    auto nowTime = chrono::high_resolution_clock::now();
    // 计算时间差
    chrono::duration<double, std::milli> time_span = chrono::duration_cast<chrono::duration<double>>(nowTime - timePoint);
    double time = time_span.count();
    // 计算速率
    double velo = (double)bytes / time / 1024 * 1000;
    // 发出信号,更新速率
    char buff[100];
    sprintf(buff, "Instant V: %.2f KB/s", velo);
    QString s(buff);
    emit sendV(s);
    // 更新时间点
    timePoint = nowTime;
}

/// @brief 输出平均速率
/// @param bytes 
void workThread::showAvgV(int bytes)
{
    // 计算时间差
    chrono::duration<double, std::milli> time_span = chrono::duration_cast<chrono::duration<double>>(endTime - startTime);
    double time = time_span.count();
    // 计算速率
    double velo = (double)bytes / time / 1024 * 1000;
    // 发出信号,更新速率
    char buff[100];
    sprintf(buff, "Average V: %.2f KB/s", velo);
    QString s(buff);
    emit sendV(s);
}


bool workThread::Request(char *ip, int port, char *fileName, int op, int mode)
{
    const char *TransferMode[2] = {"netascii", "octet"};
     // 初始化目的地址
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.S_un.S_addr = inet_addr(ip);
    // 初始化客户端套接字
    SOCKET clientSock = socket(AF_INET, SOCK_DGRAM, 0);
    if (clientSock == -1)
    {
        write(Record(0, 1, "socket error"));
        return -1;
    }
    // 请求信息
    char reqBuff[1024];
    int reqLen;
    memset(reqBuff, 0, sizeof(reqBuff));

    // 接收信息
    unsigned char recvBuff[1024];
    int recvLen;
    memset(recvBuff, 0, 1024);
        // 发送请求信息 0 + op + filename + 0 + mode + 0
    sprintf(reqBuff, "%c%c%s%c%s", 0, op, fileName, 0, TransferMode[mode]);
    reqLen = strlen(fileName) + strlen(TransferMode[mode]) + 4;

     // 超时处理
    struct timeval tv;
    fd_set readfds;
    int errNum = 0;

    // 打开文件
    FILE *fp;
    unsigned short ackNum = 0;
    if (op == TFTP_OPCODE_RRQ && mode == TFTP_NETASCII)
        fp = fopen(fileName, "w");
    else if (op == TFTP_OPCODE_RRQ && mode == TFTP_OCTET)
        fp = fopen(fileName, "wb");
    else if (op == TFTP_OPCODE_WRQ && mode == TFTP_NETASCII)
        fp = fopen(fileName, "r");
    else if (op == TFTP_OPCODE_WRQ && mode == TFTP_OCTET)
        fp = fopen(fileName, "rb");

    if (fp == nullptr)
    {
        write(Record(0, 1, "open file error"));
        return -1;
    }

    if( op == TFTP_OPCODE_RRQ)
    write(Record(0, 0, "start receive"));
    else if (op == TFTP_OPCODE_WRQ)
    write(Record(0, 0, "start sendto"));

    int bytes = 0;
    int totalBytes = 0;
    this->startV();
    int i;
    for(i = 0; ; )
    {
        // 测速器输出速率, 每1000个包输出一次
        if(i % 1000 == 0)
        {
            this->showInsV(bytes);
            bytes = 0;
        }
        // 发送请求
        int  err = sendto(clientSock, reqBuff, reqLen, 0, (struct sockaddr *)&addr, sizeof(addr));
        if (err == -1)
        {
            write(Record(0, 1, "sendto error"));
            return -1;
        }
        // 超时处理
        FD_ZERO(&readfds);
        FD_SET(clientSock, &readfds);
        tv.tv_sec = TFTP_TIMEOUT;
        tv.tv_usec = 0;
        select(clientSock + 1, &readfds, NULL, NULL, &tv);

        if (FD_ISSET(clientSock, &readfds))
        {
            // 接收数据
            struct sockaddr_in from;
            int fromlen = sizeof(from);
            recvLen = recvfrom(clientSock, (char *)recvBuff, 1024, 0, (struct sockaddr *)&from, &fromlen);
            if (recvLen > 0)
            {
                errNum = 0;
                // 更改目的地址
                memcpy((struct sockaddr *)&addr, (struct sockaddr *)&from, sizeof(fromlen));
                // 获取分组号
                int blockNum = (recvBuff[2] << 8) + recvBuff[3];
                // 获取操作码
                int opcode = (recvBuff[0] << 8) + recvBuff[1];
                // 判断操作类型
                if (op == TFTP_OPCODE_RRQ)
                // 若为读取操作
                {
                    if (opcode == TFTP_OPCODE_DATA)
                    // 判断操作码
                    {
                        // 若操作码为数据分组
                        if(blockNum == ((ackNum + 1) & 0xffff))
                        {
                            i ++;
                            ackNum++;
                            // 构造下一个包
                            sprintf(reqBuff, "%c%c%c%c", 0, TFTP_OPCODE_ACK, recvBuff[2], recvBuff[3]);
                            reqLen = 4;
                            // 写入文件
                            fwrite(recvBuff + 4, 1, recvLen - 4, fp);
                            // 计算字节数
                            bytes += recvLen - 4;
                            totalBytes += recvLen - 4;
                            // 判断是否为最后一个分组
                            if (recvLen < 516)
                            {
                                sendto(clientSock, reqBuff, reqLen, 0, (struct sockaddr *)&addr, sizeof(addr));
                                break;
                            }
                        }
                        else
                        {
                            // 若分组号不对, 重发上一个包
                            char errMsg[100];
                            sprintf(errMsg, "%d Lost, transform again", ackNum+1);
                            write(Record(3, 1, errMsg));
                        }
                    }
                    else if (opcode == TFTP_OPCODE_ERROR)
                    // 若操作码错误
                    {
                        char errMsg[100];
                        sprintf(errMsg, "error code: %d", recvBuff[3]);
                        write(Record(3, 1, errMsg));
                        clear(fp, clientSock);
                        return -1;
                    }
                    else
                    // 若操作码未知
                    {
                        write(Record(3, 1, "unknown opcode"));
                        clear(fp, clientSock);
                        return -1;
                    }
                }
                if (op == TFTP_OPCODE_WRQ)
                // 若为写操作
                {
                    // 判断操作码
                    if (opcode == TFTP_OPCODE_ACK)
                    {
                        if(blockNum == ackNum)
                        // 判断分组号
                        {
                            i ++;
                            ackNum++;
                            // 读取文件,构建数据包
                            sprintf(reqBuff, "%c%c%c%c", 0, TFTP_OPCODE_DATA, ackNum >> 8, ackNum & 0xff);
                            reqLen = fread(reqBuff + 4, 1, TFTP_DATA_SIZE, fp);
                            if (reqLen == 0)
                            {
                                break;
                            }
                            // 计算字节数
                            reqLen += 4;
                            bytes += reqLen;
                            totalBytes += reqLen;
                        }
                        else
                        {
                            char errMsg[100];
                            sprintf(errMsg, "%d Lost, transform again", ackNum-1);
                            write(Record(3, 1, errMsg));
                        }

                    }
                    else if (opcode == TFTP_OPCODE_ERROR)
                    {
                        char errMsg[100];
                        sprintf(errMsg, "error code: %d", recvBuff[3]);
                        write(Record(3, 1, errMsg));
                        clear(fp, clientSock);
                        return -1;
                    }
                    else
                    {
                        write(Record(3, 1, "unknown opcode"));
                        clear(fp, clientSock);
                        return -1;
                    }
                }
            }
        }
        else
        {
            // 超时处理
            // 写入错误日志
            write(Record(3, 1, "timeout"));
            errNum++;
            if (errNum >= TFTP_RETRY)
            // 超过重试次数
            {
                write(Record(2, 1, "retry too many times"));
                clear(fp, clientSock);
                return -1;
            }
        }
    }
    this->stopV();
    this->showAvgV(totalBytes);
    write(Record(1, 0, "transfer complete, total blocks: " + to_string(i) + ", total bytes: " + to_string(totalBytes)));
    clear(fp, clientSock);
    return true;
}

/// @brief 工作线程配置
/// @param ip 
/// @param port 
/// @param fileName 
/// @param op 
/// @param mode 
void workThread::set(char *ip, int port, char *fileName, int op, int mode)
{
    strcpy(this->serverIP, ip);
    this->port = port;
    strcpy(this->fileName, fileName);
    this->op = op;
    this->mode = mode;
}


void workThread::run()
{
    this->Request(this->serverIP, this->port, this->fileName, this->op, this->mode);
}

/// @brief 日志写入
/// @param record 
void workThread::write(Record record)
{
    // 构建日志信息字符串
    std::string msg = record.toString() + "\n";
    // 写入日志文件
    fwrite(msg.c_str(), sizeof(char), msg.length(), fp);
    // 发出信号,更新界面
    QString s(msg.c_str());
    emit sendM(s);
}

/// @brief 日志读出
/// @param records 
void workThread::read(std::vector<Record> &records)
{
    char tmp[1024];
    while (fgets(tmp, 1024, fp) != nullptr)
    {
        Record record(tmp);
        records.push_back(record);
    }
}