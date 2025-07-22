#include "chatgptclient.h"
#include <QNetworkReply>
#include <QPixmap>
#include <QMessageBox>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkRequest>
#include <QNetworkReply>
ChatGPTClient::ChatGPTClient(QObject *parent)
    : AIClient{parent}
{
    connect(networkManager,&QNetworkAccessManager::finished,this,&ChatGPTClient::onNetworkReplyFinished);

}

void ChatGPTClient::sendPicGenerationRequest(const QString &prompt)
{
    if(ChatGPT_Key.isEmpty()){
        QMessageBox::information(nullptr,"Info","Current ChatGPT API Key is empty.");
        return;
    }

    QUrl url("https://llmxapi.com/v1/images/generations"); //GPT-4o api


    QNetworkRequest request(url);
    request.setRawHeader("Authorization", "Bearer " + ChatGPT_Key.toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");

    QJsonObject jsonRequest;
    jsonRequest["prompt"] = prompt;   // 图片生成的描述
    jsonRequest["n"] = 1;             // 请求生成 1 张图片
    jsonRequest["size"] = "1024x1024";

    QJsonDocument doc(jsonRequest);
    QByteArray data = doc.toJson();

    QNetworkReply* reply=networkManager->post(request,data);
    requestTypeMap.insert(reply,RequestType::PicGeneration);
    qDebug()<<"Pic generation request sent to OpenAI"<<QString(data);
}

void ChatGPTClient::onNetworkReplyFinished(QNetworkReply *reply)
{
    //Entered slot func
    //delete the mapping
    RequestType type=requestTypeMap.value(reply);
    requestTypeMap.remove(reply);
    //check error and emit signal
    if(reply->error()!=QNetworkReply::NoError){
        emit errorOccured("Network error: " + reply->errorString());
        reply->deleteLater();
        return;
    }

    //get data
    QByteArray data=reply->readAll();
    QJsonDocument doc=QJsonDocument::fromJson(data);
    qDebug() << "Received data from ChatGPT:" << QString(data);

    QString extractedText;
    if(type==RequestType::PicGeneration){
        if(doc.isObject()){
            QJsonObject obj=doc.object();
            if(obj.contains("data")&&obj["data"].isArray()){
                QJsonArray dataArray=obj["data"].toArray();
                if(!dataArray.isEmpty()&&dataArray[0].isObject()){
                    QJsonObject urlObj=dataArray[0].toObject();
                    if(urlObj.contains("url")&&urlObj["url"].isString()){
                        QString urlInString=urlObj["url"].toString();
                        qDebug() << "Image URL received: " << urlInString;


                        //download pic
                        QUrl url(urlInString);
                        QNetworkRequest request(url);
                        QNetworkReply* imageReply = networkManager->get(request);
                        connect(imageReply,&QNetworkReply::finished,[this,imageReply](){
                            QByteArray imgData=imageReply->readAll();
                            QPixmap image;
                            if(image.loadFromData(imgData)){
                                qDebug() << "Emitting imageGenerated signal.";
                                emit generatedImgReceived(image);
                            }
                            else{
                                emit errorOccured("Failed to load image from URL.");
                            }
                            imageReply->deleteLater();
                        });
                    }else {
                        emit errorOccured("Image URL not found in response.");
                    }
                } else {
                    emit errorOccured("No image data found in response.");
                }
            } else {
                emit errorOccured("Invalid response format (no 'data' array).");
            }
        } else {
            emit errorOccured("Invalid response format (not an object).");
        }
    }else{
        if(doc.isObject()){
            QJsonObject obj=doc.object();
            if(obj.contains("choices")&&obj["choices"].isArray()){
                QJsonArray choices = obj["choices"].toArray();
                for (const QJsonValue &choiceValue : choices){
                    if (choiceValue.isObject()) {
                        QJsonObject choiceObject = choiceValue.toObject();
                        // 检查是否包含"message"字段并且是一个对象
                        if (choiceObject.contains("message") && choiceObject["message"].isObject()) {
                            QJsonObject gptReplyObject = choiceObject["message"].toObject();

                            // 检查是否包含"content"字段并且是一个字符串
                            if (gptReplyObject.contains("content") && gptReplyObject["content"].isString()) {
                                extractedText = gptReplyObject["content"].toString().trimmed();
                                qDebug()<<"extracted text"<<extractedText;
                                // 额外的处理（例如标题生成或重写）
                                if (type == RequestType::TitleGernation || type == RequestType::Rewrite) {
                                    if (extractedText.startsWith("\"") && extractedText.endsWith("\"")) {
                                        extractedText = extractedText.mid(1, extractedText.length() - 2);
                                    }
                                }
                            }

                        }

                    }
                    // **修改：一旦提取到有效文本，立刻跳出循环**
                    if(!extractedText.isEmpty()) {
                        break;  // 跳出循环，避免重复处理
                    }
                }
            }
        }
    }
    if(extractedText.isEmpty()){
        emit errorOccured("Failed to extract valid response from AI. Raw: "+QString(data));
    }
    if(type==RequestType::TitleGernation){
        qDebug() << "Emitting aiResponseReceived signal with response: " << extractedText;
        emit titleGenerated(extractedText);
    }
    else if(type == RequestType::ChatMessage){
        qDebug() << "Emitting aiResponseReceived signal with response: " << extractedText;
        emit aiResponseReceived(extractedText);
    }else if(type ==RequestType::Rewrite){
        qDebug() << "Emitting aiResponseReceived signal with response: " << extractedText;
        emit rewritedContentReceived(extractedText);
    }
    reply->deleteLater();
}
void ChatGPTClient::setApiKey(const QString &key)
{
    ChatGPT_Key=key;
}

void ChatGPTClient::sendMessage(const QString &userInput, const QJsonArray &conversationHistory)
{
    if(ChatGPT_Key.isEmpty()){
        QMessageBox::information(nullptr,"Info","Current ChatGPT API Key is empty.");
        return;
    }
    QUrl url("https://llmxapi.com/v1/chat/completions"); //GPT-4o api

    QNetworkRequest request(url);
    request.setRawHeader("Authorization", "Bearer " + ChatGPT_Key.toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");

    QJsonArray chatContents = builtChatGPTContents(conversationHistory, userInput);
    //build request struct
    QJsonObject requestBody;
    requestBody["model"]="gpt-4o";// 选择使用的GPT模型
    requestBody["messages"] = chatContents;  // 将生成的消息内容添加到请求体
    requestBody["temperature"] = 0.7;  // 设置温度，控制生成的随机性
    requestBody["max_tokens"] = 1000;   // 最大token数，可以根据需求调整
    requestBody["top_p"] = 1;          // 设置top_p，控制采样的多样性
    requestBody["frequency_penalty"] = 0;  // 设置频率惩罚
    requestBody["presence_penalty"] = 0;   // 设置出现频率惩罚

    QByteArray requestData = QJsonDocument(requestBody).toJson();
    QNetworkReply* reply=networkManager->post(request,requestData);
    requestTypeMap.insert(reply,RequestType::ChatMessage);
    qDebug() << "Sending chat message to ChatGPT:" << QString(requestData);
}

void ChatGPTClient::requestConversationTitle(const QString &firstMessage)
{
    if(ChatGPT_Key.isEmpty()){
        QMessageBox::information(nullptr,"Info","Current ChatGPT API Key is empty.");
        return;
    }
    QUrl url("https://llmxapi.com/v1/chat/completions"); //GPT-4o api

    QNetworkRequest request(url);
    request.setRawHeader("Authorization", "Bearer " + ChatGPT_Key.toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");

    QJsonArray messages;
    QJsonObject userMessage;
    userMessage["role"] = "user";
    userMessage["content"] = "Generate a concise, descriptive title (max 8 words, must same language with the user) "
                             "for a conversation that starts with: \"" + firstMessage + "\". "
                                              "Just the title, no extra text.";
    messages.append(userMessage);

    QJsonObject requestBody;
    requestBody["model"]="gpt-4o";// 选择使用的GPT模型
    requestBody["messages"] = messages;  // 将生成的消息内容添加到请求体
    requestBody["temperature"] = 0.7;  // 设置温度，控制生成的随机性
    requestBody["max_tokens"] = 100;   // 最大token数，可以根据需求调整
    requestBody["top_p"] = 1;          // 设置top_p，控制采样的多样性
    requestBody["frequency_penalty"] = 0;  // 设置频率惩罚
    requestBody["presence_penalty"] = 0;   // 设置出现频率惩罚

    QByteArray requestData = QJsonDocument(requestBody).toJson();
    QNetworkReply* reply=networkManager->post(request,requestData);
    requestTypeMap.insert(reply,RequestType::TitleGernation);
    qDebug() << "Sending title generation request to ChatGPT:" << QString(requestData);
}

void ChatGPTClient::sendRewriteRequest(const QString &usersClipBoardText, const QString &usersDemand)
{
    if(ChatGPT_Key.isEmpty()){
        QMessageBox::information(nullptr,"Info","Current ChatGPT API Key is empty.");
        return;
    }
    QUrl url("https://llmxapi.com/v1/chat/completions"); //GPT-4o api

    QNetworkRequest request(url);
    request.setRawHeader("Authorization", "Bearer " + ChatGPT_Key.toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");

    QJsonArray messages;
    QJsonObject userMessage;
    userMessage["role"] = "user";
    userMessage["content"] = "Rewrite the current text to make it more appropriate "
                             "based on the user's input and the specific context. (Must be same language as user's) "
                             "If the user has a specific rewriting direction, please follow the user's rewriting direction.(ONLY returns the most appropriate anwser(1 ONLY), NO OTHER TEXT)"
                             "Starts with: \"" + usersClipBoardText + "\" ; User's rewriting direction: " + usersDemand;
    messages.append(userMessage);
    //build request struct
    QJsonObject requestBody;
    requestBody["model"]="gpt-4o";// 选择使用的GPT模型
    requestBody["messages"] = messages;  // 将生成的消息内容添加到请求体
    requestBody["temperature"] = 0.7;  // 设置温度，控制生成的随机性
    //requestBody["max_tokens"] = 100;   // 最大token数，可以根据需求调整
    requestBody["top_p"] = 1;          // 设置top_p，控制采样的多样性
    requestBody["frequency_penalty"] = 0;  // 设置频率惩罚
    requestBody["presence_penalty"] = 0;   // 设置出现频率惩罚

    QByteArray requestData = QJsonDocument(requestBody).toJson();
    QNetworkReply* reply=networkManager->post(request,requestData);
    requestTypeMap.insert(reply,RequestType::Rewrite);
    qDebug() << "Sending text rewrite request to ChatGPT:" << QString(requestData);
}

QJsonArray ChatGPTClient::builtChatGPTContents(const QJsonArray &conversationHistory, const QString &currentUserInput)
{
    QJsonArray contentArray;
    for(const QJsonValue& value: conversationHistory){
        if(value.isObject()){
            QJsonObject turnToObject=value.toObject();
            if(turnToObject.contains("user")&&turnToObject["user"].isString()){
                QJsonObject userPart;
                userPart["role"]="user";
                userPart["content"]=turnToObject["user"].toString();
                contentArray.append(userPart);
            }

            if(turnToObject.contains("ai")&&turnToObject["ai"].isString()){
                QJsonObject modelPart;
                modelPart["role"]="assistant";
                modelPart["content"]=turnToObject["content"].toString();
                contentArray.append(modelPart);

            }

        }
    }
    //if new input from user, append to the last
    if(!currentUserInput.isEmpty()){
        QJsonObject userPart;
        userPart["role"]="user";
        userPart["content"]=currentUserInput;

        contentArray.append(userPart);
    }
    return contentArray;
}
