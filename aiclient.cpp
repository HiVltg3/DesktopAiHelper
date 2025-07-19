#include "aiclient.h"

AIClient::AIClient(QObject *parent)
    : QObject{parent}
{
    networkManager = new QNetworkAccessManager(this);
}
