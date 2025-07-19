#ifndef GEMINICLIENT_H
#define GEMINICLIENT_H

#include "aiclient.h"

class GeminiClient : public AIClient
{
    Q_OBJECT
private:
    QString Gemini_Key; //store api key
    enum class RequestType{
        ChatMessage,
        TitleGernation,
        Rewrite
    };
    QMap<QNetworkReply*,RequestType> requestTypeMap;
    QJsonArray builtGeminiContents(const QJsonArray& conversationHistory,const QString& currentUserInput="");
public:
    explicit GeminiClient(QObject *parent = nullptr);
    // virtual functions
    void setApiKey(const QString& key) override;
    void sendMessage(const QString& userInput,const QJsonArray& conversationHistory) override;
    void requestConversationTitle(const QString& firstMessage) override;
    void sendRewriteRequest(const QString &usersClipBoardText,const QString& usersDemand="") override;
private slots:
    void onNetworkReplyFinished(QNetworkReply *reply);

    // AIClient interface

};


#endif // GEMI  ICLIENT_H
