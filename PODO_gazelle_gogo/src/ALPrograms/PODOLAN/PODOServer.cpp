#include "PODOServer.h"

PODO_GUI_Server::PODO_GUI_Server()
{
    //dataSize = sizeof(USER_COMMAND);
    dataSize = sizeof(LAN_GUI2PODO);
    dataReceived.clear();
    RBData.resize(dataSize);
}

void PODO_GUI_Server::ReadData(){
    QDataStream in(RBTcpClient);
    in.setVersion(QDataStream::Qt_5_2);

    if(RBTcpClient->bytesAvailable() < dataSize)
        return;

    while(RBTcpClient->bytesAvailable() >= dataSize){
        in.readRawData(RBData.data(), dataSize);
        dataReceived.push_back(RBData);
    }
}

