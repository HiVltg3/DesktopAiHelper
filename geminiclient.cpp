#include "geminiclient.h"
#include <QJsonArray>
#include <QJsonValue>
#include <QJsonObject>
#include <QMessageBox>
#include <QUrlQuery>
#include <QNetworkReply>
#include <QMessageBox>
GeminiClient::GeminiClient(QObject *parent)
    : AIClient{parent}
{
    connect(networkManager,&QNetworkAccessManager::finished,this,&GeminiClient::onNetworkReplyFinished);

}

void GeminiClient::setApiKey(const QString &key)
{
    Gemini_Key=key;
}

QJsonArray GeminiClient::builtGeminiContents(const QJsonArray &conversationHistory, const QString &currentUserInput)
{
    QJsonArray contentArray;
    for(const QJsonValue& value: conversationHistory){
        if(value.isObject()){
            QJsonObject turnToObject=value.toObject();
            if(turnToObject.contains("user")&&turnToObject["user"].isString()){
                QJsonObject userPart;
                QJsonArray userPartArray;
                userPart["text"]=turnToObject["user"].toString();
                userPartArray.append(userPart);

                QJsonObject userContent;
                userContent["role"]="user";
                userContent["parts"]=userPartArray;
                contentArray.append(userContent);
            }

            if(turnToObject.contains("ai")&&turnToObject["ai"].isString()){
                QJsonObject modelPart;
                QJsonArray modelPartArray;
                modelPart["text"]=turnToObject["ai"].toString();
                modelPartArray.append(modelPart);

                QJsonObject modelContent;
                modelContent["role"]="model";
                modelContent["parts"]=modelPartArray;
                contentArray.append(modelContent);
            }

        }
    }
    //if new input from user, append to the last
    if(!currentUserInput.isEmpty()){
        QJsonObject userPart;
        QJsonArray userPartArray;
        userPart["text"]=currentUserInput;
        userPartArray.append(userPart);

        QJsonObject userContent;
        userContent["role"]="user";
        userContent["parts"]=userPartArray;
        contentArray.append(userContent);
    }
    return contentArray;
}

void GeminiClient::sendMessage(const QString &userInput, const QJsonArray &conversationHistory)
{
    if(Gemini_Key.isEmpty()){
        QMessageBox::information(nullptr,"Info","Current Gemini API Key is empty.");
        return;
    }

    //build request url
    QUrl url("https://llmxapi.com/v1beta/models/gemini-2.5-flash:generateContent");
    QUrlQuery query;
    query.addQueryItem("key",Gemini_Key);
    url.setQuery(query);

    //setup data and request
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QJsonObject jsonRequest;
    jsonRequest["contents"]=builtGeminiContents(conversationHistory,userInput);
    // can set parameters such as temperature, maxOutputTokens
    // jsonRequest["temperature"] = 0.7;
    // jsonRequest["maxOutputTokens"] = 500; // set the maximum reply length
    QJsonDocument doc(jsonRequest);
    QByteArray data=doc.toJson();

    //post previous conversation to Gemini
    QNetworkReply* reply=networkManager->post(request,data);
    requestTypeMap.insert(reply,RequestType::ChatMessage);
    qDebug() << "Sending chat message to Gemini:" << QString(data);
}

void GeminiClient::requestConversationTitle(const QString &firstMessage)
{
    if(Gemini_Key.isEmpty()){
        QMessageBox::information(nullptr,"Info","Current Gemini API Key is empty.");
        return;
    }
    QUrl url("https://llmxapi.com/v1beta/models/gemini-2.5-flash:generateContent");
    QUrlQuery query;
    query.addQueryItem("key",Gemini_Key);
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");

    QJsonObject userPart;
    QJsonArray userPartArray;
    QString prompt = "Generate a concise, descriptive title (max 8 words, must same language with the user) "
                     "for a conversation that starts with: \"" + firstMessage + "\". "
                                      "Just the title, no extra text.";
    userPart["text"]=prompt;
    userPartArray.append(userPart);

    QJsonObject userContent;
    userContent["role"]="user";
    userContent["parts"]=userPartArray;

    QJsonArray contentsArray;
    contentsArray.append(userContent);

    QJsonObject jsonRequest;
    jsonRequest["contents"]=contentsArray;
    jsonRequest["maxOutputTokens"]=20; //limit the title length
    jsonRequest["temperature"]=0.3;//low temperature can be more sure about the title

    QJsonDocument doc(jsonRequest);
    QByteArray data=doc.toJson();

    QNetworkReply* reply=networkManager->post(request,data);
    requestTypeMap.insert(reply,RequestType::TitleGernation); //mark the title generation request
    qDebug() << "Sending title generation request to Gemini:" << QString(data);
}

void GeminiClient::onNetworkReplyFinished(QNetworkReply *reply)
{
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
    qDebug() << "Received data from Gemini:" << QString(data);

    QString extractedText;
    if(type==RequestType::PicGeneration){
        if(doc.isObject()){
            QJsonObject obj=doc.object();
            if(obj.contains("predictions")&&obj["predictions"].isArray()){
                QJsonArray predictionsArray=obj["predictions"].toArray();
                if(!predictionsArray.isEmpty()&&predictionsArray[0].isObject()){
                    QJsonObject predictionObj=predictionsArray[0].toObject();
                    if(predictionObj.contains("bytesBase64Encoded")&&predictionObj["bytesBase64Encoded"].isString()){
                        QString dataString=predictionObj["bytesBase64Encoded"].toString();
                        QByteArray byteData=QByteArray::fromBase64(dataString.toUtf8());//
                        QPixmap image;
                        if(image.loadFromData(byteData)){
                            qDebug()<<"Emitting imageGenerated signal.";
                            emit generatedImgReceived(image);
                        }else{
                            emit errorOccured("Failed to load image from base64 data.");
                        }
                    }else{
                        emit errorOccured("Image data not found in response.");
                    }
                }else{
                    emit errorOccured("No image predictions found in response.");
                }
            }else{
                emit errorOccured("Invalid image generation response format (no 'predictions' array).");
            }
        }else{
            emit errorOccured("Invalid image generation response format (not an object).");
        }
    }
    //text related
    else{
        if(doc.isObject()){
            QJsonObject obj=doc.object();
            if(obj.contains("candidates")&&obj["candidates"].isArray()){
                QJsonArray candidatesArray=obj["candidates"].toArray();
                if(!candidatesArray.isEmpty() && candidatesArray[0].isObject()){
                    QJsonObject candidateObj=candidatesArray[0].toObject();
                    if(candidateObj.contains("content")&&candidateObj["content"].isObject()){
                        QJsonObject contentObj=candidateObj["content"].toObject();
                        if(contentObj.contains("parts")&&contentObj["parts"].isArray()){
                            QJsonArray partsArray=contentObj["parts"].toArray();
                            if(!partsArray.isEmpty()&&partsArray[0].isObject()){
                                QJsonObject textObj=partsArray[0].toObject();
                                if(textObj.contains("text")&&textObj["text"].isString()){
                                    extractedText=textObj["text"].toString().trimmed();

                                    //extra steps for title
                                    if(type==RequestType::TitleGernation||type==RequestType::Rewrite || type == RequestType::Translate){
                                        if(extractedText.startsWith("\"")&&extractedText.endsWith("\"")){
                                            extractedText=extractedText.mid(1,extractedText.length()-2);
                                        }
                                    }
                                }
                            }
                        }
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
    else if(type == RequestType::Translate){
        qDebug() << "Emitting translateContentReceived signal with response: " << extractedText;
        emit translateContentReceived(extractedText);
    }
}
void GeminiClient::sendRewriteRequest(const QString &usersClipBoardText, const QString &usersDemand)//rewrite func, under construction
{
    if(Gemini_Key.isEmpty()){
        QMessageBox::information(nullptr,"Info","Current Gemini API Key is empty.");
        return;
    }

    QUrl url("https://llmxapi.com/v1beta/models/gemini-2.5-flash:generateContent");
    QUrlQuery query;
    query.addQueryItem("key", Gemini_Key);
    url.setQuery(query);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    // Prepare the JSON structure
    QString prompt = "Rewrite the current text to make it more appropriate "
                     "based on the user's input and the specific context. (Must be same language as user's) "
                     "If the user has a specific rewriting direction, please follow the user's rewriting direction.(ONLY returns the most appropriate anwser(1 ONLY), NO OTHER TEXT)"
                     "Starts with: \"" + usersClipBoardText + "\" ; User's rewriting direction: " + usersDemand;

    QJsonArray partsArray;
    QJsonObject textPart;
    textPart["text"] = prompt; // insert the comeplete promtp into text
    partsArray.append(textPart);

    // Constructing a content object: including roles and parts
    QJsonObject userContent;
    userContent["role"] = "user"; // sender is user
    userContent["parts"] = partsArray; // Contains the parts array constructed above

    // Construct the contents array: Contains all content objects (there is only one round of user request here)
    QJsonArray contentsArray;
    contentsArray.append(userContent);

    // Construct the final JSON request object
    QJsonObject jsonRequest;
    jsonRequest["contents"] = contentsArray;
    //parameters
    QJsonObject generationConfig;
    generationConfig["temperature"] = 0.3;
    jsonRequest["generationConfig"] = generationConfig;

    QJsonDocument doc(jsonRequest);
    QByteArray data = doc.toJson();

    // Sending the request
    QNetworkReply* reply = networkManager->post(request, data);
    requestTypeMap.insert(reply, RequestType::Rewrite);
    qDebug() << "Sending the rewrite request to Gemini:" << QString(data);
}

//Translate
void GeminiClient::sendTranslateRequest(const QString &usersClipBoardText, const QString &targetLanguage)
{
    if(Gemini_Key.isEmpty()){
        QMessageBox::information(nullptr,"Info","Current Gemini API Key is empty.");
        return;
    }

    QUrl url("https://llmxapi.com/v1beta/models/gemini-2.5-flash:generateContent");
    QUrlQuery query;
    query.addQueryItem("key", Gemini_Key);
    url.setQuery(query);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    // Prepare the JSON structure
    QString prompt = "Translate user's text to targeted language(default target language is English)"
                     "(ONLY returns the translated text, NO OTHER TEXT)"
                     "Starts with: \"" + usersClipBoardText + "\" ; Target language: " + targetLanguage;

    QJsonArray partsArray;
    QJsonObject textPart;
    textPart["text"] = prompt; // insert the comeplete promtp into text
    partsArray.append(textPart);

    // Constructing a content object: including roles and parts
    QJsonObject userContent;
    userContent["role"] = "user"; // sender is user
    userContent["parts"] = partsArray; // Contains the parts array constructed above

    // Construct the contents array: Contains all content objects (there is only one round of user request here)
    QJsonArray contentsArray;
    contentsArray.append(userContent);

    // Construct the final JSON request object
    QJsonObject jsonRequest;
    jsonRequest["contents"] = contentsArray;
    //parameters
    QJsonObject generationConfig;
    generationConfig["temperature"] = 0.3;
    jsonRequest["generationConfig"] = generationConfig;

    QJsonDocument doc(jsonRequest);
    QByteArray data = doc.toJson();

    // Sending the request
    QNetworkReply* reply = networkManager->post(request, data);
    requestTypeMap.insert(reply, RequestType::Rewrite);
    qDebug() << "Sending the rewrite request to Gemini:" << QString(data);
}

//generate pics
void GeminiClient::sendPicGenerationRequest(const QString &prompt)
{
    if(Gemini_Key.isEmpty()){
        QMessageBox::information(nullptr,"Info","Current Gemini API Key is empty.");
        return;
    }

    //!!!the api link is closed now, unable to test, please move to chatgpt
    QUrl url("https://llmxapi.com/v1beta/models/gemini-2.0-flash-preview-image-generation:generateContent");
    QUrlQuery query;
    query.addQueryItem("key", Gemini_Key);
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");

    QJsonObject instancesObj;
    instancesObj["prompt"]=prompt;
    QJsonArray instancesArray;
    instancesArray.append(instancesObj);

    QJsonObject parametersObj;
    parametersObj["sampleCount"]=1;//generate 1 pic

    QJsonObject jsonRequest;
    jsonRequest["instances"]=instancesArray;
    jsonRequest["parameters"]=parametersObj;

    QJsonDocument doc(jsonRequest);
    QByteArray data = doc.toJson();

    QNetworkReply* reply=networkManager->post(request,data);
    requestTypeMap.insert(reply,RequestType::PicGeneration);
    qDebug()<<"Pic generation request sent to Gemini"<<QString(data);
}
