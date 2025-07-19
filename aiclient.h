#ifndef AICLIENT_H
#define AICLIENT_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QString>
#include <QJsonArray>
class AIClient : public QObject
{
    Q_OBJECT
public:
    explicit AIClient(QObject *parent = nullptr);
    virtual ~AIClient()=default;

    //set API Key
    virtual void setApiKey(const QString& key)=0;

    //send messages,load prompt to ai
    virtual void sendMessage(const QString& userInput,const QJsonArray& conversationHistory)=0;

    //request title for new conversations
    virtual void requestConversationTitle(const QString& firstMessage)=0;

    //request to rewrite
    virtual void sendRewriteRequest(const QString& usersClipBoardText,const QString& usersDemand="")=0;
signals:
    //get ai reply
    void aiResponseReceived(const QString& response);
    //error signal
    void errorOccured(const QString& errorMessage);
    //title generate for new dialog
    void titleGenerated(const QString& title);
    //rewrite content return
    void rewritedContentReceived(const QString& rewritedContent);
protected:
    QNetworkAccessManager* networkManager;
};

#endif // AICLIENT_H
