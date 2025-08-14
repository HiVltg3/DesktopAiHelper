#ifndef CHATGPTCLIENT_H
#define CHATGPTCLIENT_H

#include "aiclient.h"

class ChatGPTClient : public AIClient
{
    Q_OBJECT
private:
    QString ChatGPT_Key; //store api key
    QMap<QNetworkReply*,RequestType> requestTypeMap;
    QJsonArray builtChatGPTContents(const QJsonArray &conversationHistory, const QString &currentUserInput);
public:
    explicit ChatGPTClient(QObject *parent = nullptr);
    void sendPicGenerationRequest(const QString &prompt) override;
    //haven't done yet
    virtual void setApiKey(const QString &key) override;
    virtual void sendMessage(const QString &userInput, const QJsonArray &conversationHistory) override;
    virtual void requestConversationTitle(const QString &firstMessage) override;
    virtual void sendRewriteRequest(const QString &usersClipBoardText, const QString &usersDemand) override;
    virtual void sendTranslateRequest(const QString &usersClipBoardText, const QString &targetLanguage="English") override;
private slots:
    void onNetworkReplyFinished(QNetworkReply *reply);

    // AIClient interface


    // AIClient interface
};

#endif // CHATGPTCLIENT_H
