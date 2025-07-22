#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QListView>
#include <QItemSelectionModel>
#include <QStandardItemModel>
#include <QListView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QJsonArray>
#include <QDir>
#include <QSplitter>
#include <QPalette>
#include <QInputDialog>
#include <QTimer>
#include <QClipboard>
#include <QTextToSpeech>
#include <QBuffer>
#include <QKeyEvent>
#include "geminiclient.h"
#include "chatgptclient.h"
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    UISetup();
    setupEventFilter();
    //GeminiSetup();
    ChatGPTSetup();
    //rewrite detection tool
    QClipboard* clipboard=QApplication::clipboard();
    previousClipboardText=clipboard->text();
    startClipboardMonitoring();
}

MainWindow::~MainWindow()
{
    saveChatHistory();
    delete popup;
    delete ui;
}


//functions begin here
//JSON
void MainWindow::loadChatHistory(QStandardItemModel *model)
{
    //load local json file
    QString currentDir = QDir::currentPath();
    qDebug() << "Current working directory: " << currentDir; //current is under build directory, will change it once the project is finished


    QFile chatHistoryDocument("chathistory.json");
    if(!chatHistoryDocument.exists()){
        QMessageBox::warning(this, "Warning", "The chat history file does not exist!");
        //if the file does not exist, create one
        QJsonObject rootObj;
        rootObj["conversations"]=QJsonArray();
        QJsonDocument doc(rootObj);
        chatHistoryDocument.open(QIODevice::WriteOnly | QIODevice::Truncate);
        chatHistoryDocument.write(doc.toJson());
        chatHistoryDocument.close();
        return;
    }
    if(!chatHistoryDocument.open(QIODevice::ReadOnly)){
        QMessageBox::warning(this,"Warning","Open json file failed!");
        return;
    }
    QByteArray data=chatHistoryDocument.readAll();
    QJsonDocument doc= QJsonDocument::fromJson(data);
    chatHistoryDocument.close();
    if(!doc.isObject()){
        QMessageBox::warning(this,"Warning","Load chat history failed! (Not an object)");
        return;
    }
    QJsonObject obj = doc.object();
    if(!obj.contains("conversations")||!obj["conversations"].isArray()){
        QMessageBox::warning(this,"Warning","Load chat history failed! (No conversations array)");
        return;
    }

    // store all data to modify or save in future
    allConversations = obj["conversations"].toArray();

    //loop to get data
    int i=0;
    for(const QJsonValue& value:obj["conversations"].toArray()){
        //get individual conversations
        QJsonObject chatObject=value.toObject();
        //get title
        QString chatTitle=chatObject["title"].toString();
        QStandardItem* item=new QStandardItem(chatTitle);
        item->setData(i,Qt::UserRole+1);// save the index!!!
        i++;
        model->appendRow(item); //add conversation title to list view

        //get dialog
        QJsonArray dialogArray=chatObject["dialogue"].toArray();
        QString content;
        QString textColor=getTextColorBasedOnTheme();
        for(const QJsonValue& dialogValue:dialogArray ){
            //add dialog to main page
            QString userMessage=dialogValue["user"].toString();
            QString AIMessage=dialogValue["ai"].toString();

            // Format as HTML, add to content
            content += "<div style='text-align:right; color:"+textColor+";'><b>User:</b> " + userMessage + "</div><br>";
            content += "<div style='text-align:left; color:"+textColor+";'><b>AI:</b> " + AIMessage + "</div><br><br>";
            item->setData(content,Qt::UserRole);
        }
    }
}

//change the conversation text color base on windows theme
QString MainWindow::getTextColorBasedOnTheme()
{
    QPalette palette=QApplication::palette();
    QColor color=palette.color(QPalette::Active, QPalette::Window);
    if(color.lightness()<128){
        return "white";
    }
    else{
        return "black";
    }
}

void MainWindow::saveChatHistory()
{
    QFile chatHistoryDocument("chathistory.json");
    if(!chatHistoryDocument.open(QIODevice::WriteOnly|QIODevice::Truncate)){
        QMessageBox::warning(this,"warning","Can't save to file");
        return;
    }
    QJsonObject rootObj;
    rootObj["conversations"]=allConversations;
    QJsonDocument doc(rootObj);
    chatHistoryDocument.write(doc.toJson(QJsonDocument::Indented));
    chatHistoryDocument.close();
}

void MainWindow::displayUserMessage(const QString &message)
{
    QString textColor=getTextColorBasedOnTheme();
    ui->AIResponse->append("<div style='text-align:right; color:"+textColor+";'><b>User:</b> " + message + "</div><br>");
}

void MainWindow::displayAIMessage(const QString &message)
{
    QString textColor=getTextColorBasedOnTheme();
    ui->AIResponse->append("<div style='text-align:left; color:"+textColor+";'><b>AI:</b> " + message + "</div><br><br>");
}

//rewrite function related
void MainWindow::startClipboardMonitoring()
{
    QTimer* timer=new QTimer(this);
    connect(timer,&QTimer::timeout,this,&MainWindow::checkClipboard);
    timer->start(1000); //check clipboard every 1 second
}

void MainWindow::showRewritePrompt(const QString &copiedText)
{
    QPoint mousePosition=QCursor::pos();//get current mouse position
    //popup the widget offer rewrite choice
    popup=new QWidget();
    popup->setWindowFlags(Qt::ToolTip|Qt::FramelessWindowHint);
    popup->setGeometry(mousePosition.x(),mousePosition.y(),200,100); //show the tool at the mouse position

    QPushButton* rewriteButton=new QPushButton("Rewrite",popup);
    QPushButton* rewriteButtonWithPrompt=new QPushButton("Rewrite with prompt",popup);
    rewriteButton->setGeometry(0,0,200,50);
    rewriteButtonWithPrompt->setGeometry(0,50,200,50);
    rewriteButton->setIcon(QIcon(":/icons/icons/magicwand.png"));
    rewriteButtonWithPrompt->setIcon(QIcon(":/icons/icons/magicwand.png"));
    connect(rewriteButton,&QPushButton::clicked,this,&MainWindow::rewriteText);
    connect(rewriteButtonWithPrompt,&QPushButton::clicked,this,&MainWindow::onRewriteWithPromptClicked);

    //hover 3s then close automatically
    QTimer::singleShot(5000, popup, &QWidget::close);
    popup->show();
}

void MainWindow::checkClipboard()
{
    QClipboard* clipboard=QApplication::clipboard();
    QString currentText=clipboard->text();

    if(currentText!=previousClipboardText){
        previousClipboardText=currentText;
        showRewritePrompt(currentText);
    }
}

void MainWindow::rewriteText()
{
    QClipboard* clipboard=QApplication::clipboard();
    QString currentText=clipboard->text();

    aiClient->sendRewriteRequest(currentText);
}

void MainWindow::onRewriteWithPromptClicked()
{
    bool ok;
    QString prompt=QInputDialog::getText(popup,"Enter Prompt", "Please enter the prompt:", QLineEdit::Normal, "", &ok);
    if(ok && !prompt.isEmpty()){
        QClipboard* clipboard=QApplication::clipboard();
        QString currentText=clipboard->text();

        if(!currentText.isEmpty()){
            aiClient->sendRewriteRequest(currentText,prompt);
        }
    }else{
        QMessageBox::warning(this, "Warning", "Prompt cannot be empty!");
    }
}

void MainWindow::UISetup()
{
    setWindowTitle("Desktop AI Tool");
    setWindowIcon(QIcon(":/icons/icons/mainIcon.png"));
    //create list for chat history
    chatHistoryModel=new QStandardItemModel(this);
    chatHistoryList=new QListView(this);
    chatHistoryList->setSelectionMode(QAbstractItemView::SingleSelection);//ban multi choose
    chatHistoryList->setEditTriggers(QAbstractItemView::NoEditTriggers);//no editing
    chatHistotySelectionModel=new QItemSelectionModel(chatHistoryModel);
    chatHistoryList->setModel(chatHistoryModel);
    chatHistoryList->setSelectionModel(chatHistotySelectionModel);
    QVBoxLayout* vBoxLayout=new QVBoxLayout(this);
    vBoxLayout->addWidget(chatHistoryList,Qt::AlignBottom|Qt::AlignLeft);
    chatHistoryList->setMinimumSize(200, 400); // Set the minimum width to 300 and the height to 400
    loadChatHistory(chatHistoryModel); //load chat history to listview

    //show conversation
    connect(chatHistoryList,&QListView::clicked,this,&MainWindow::do_showChatHistory);
    connect(chatHistoryList,&QListView::doubleClicked,this,&MainWindow::do_showChatHistory);
}

void MainWindow::GeminiSetup()
{
    //initialized Gemini client
    aiClient = new GeminiClient(this);
    aiClient->setApiKey("sk-v371No7bdNaaTYY1SoItAVRDi7o6p71BsBqaml0ABGxMpRW8");
    //connect Gemini slots
    connect(aiClient,&GeminiClient::aiResponseReceived,this,&MainWindow::handleAiResponse);
    connect(aiClient,&GeminiClient::titleGenerated,this,&MainWindow::handleTitleGenerated);
    connect(aiClient,&GeminiClient::errorOccured,this,&MainWindow::handleAiError);
    connect(aiClient,&GeminiClient::rewritedContentReceived,this,&MainWindow::handleRewritedContent);
    connect(aiClient,&GeminiClient::generatedImgReceived,this,&MainWindow::handlePicContent);

}

void MainWindow::ChatGPTSetup()
{
    aiClient=new ChatGPTClient(this);
    aiClient->setApiKey("sk-v371No7bdNaaTYY1SoItAVRDi7o6p71BsBqaml0ABGxMpRW8");
    connect(aiClient,&ChatGPTClient::generatedImgReceived,this,&MainWindow::handlePicContent);
    connect(aiClient,&ChatGPTClient::aiResponseReceived,this,&MainWindow::handleAiResponse);
    connect(aiClient,&ChatGPTClient::titleGenerated,this,&MainWindow::handleTitleGenerated);
    connect(aiClient,&ChatGPTClient::errorOccured,this,&MainWindow::handleAiError);
    connect(aiClient,&ChatGPTClient::rewritedContentReceived,this,&MainWindow::handleRewritedContent);
}

void MainWindow::setupEventFilter()
{
    ui->UserInput->installEventFilter(this);  // 安装事件过滤器
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if(watched==ui->UserInput){
        if(event->type()==QEvent::KeyPress){
            QKeyEvent* keyEvent=static_cast<QKeyEvent*>(event);
            if(keyEvent->key()==Qt::Key_Enter|| keyEvent->key() == Qt::Key_Return){
                if (keyEvent->modifiers() & Qt::ShiftModifier) {
                    // 如果按下的是 Shift + Enter，允许换行
                    return false;  // 返回 false，事件继续传递给 QPlainTextEdit，进行换行
                } else {
                    // 否则，触发按钮点击
                    ui->button_sendText->click();
                    return true;  // 返回 true，表示事件已被处理
                }
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}


//functions end here

//slots begin here
void MainWindow::do_showChatHistory(const QModelIndex &index)
{
    // //get conversation title
    // QString dialogTitle=chatHistoryModel->data(index,Qt::DisplayRole).toString();

    //get current conversation index
    currentConversationIndex=chatHistoryList->selectionModel()->currentIndex().row();
    QString conversationContent=chatHistoryModel->itemFromIndex(index)->data(Qt::UserRole).toString();
    QTextBrowser *textBrowser = ui->AIResponse;
    textBrowser->setHtml(conversationContent);
    qDebug()<<"current Index: "<<currentConversationIndex;
    // if (currentConversationIndex != -1 && currentConversationIndex < allConversations.size()){
    //     QJsonObject currentChatObject = allConversations.at(currentConversationIndex).toObject();
    //     QJsonArray currentConversationHistory = currentChatObject["dialogue"].toArray();
    // }
}

void MainWindow::on_button_sendText_clicked()
{
    userInput=ui->UserInput->toPlainText().trimmed();//save the user input
    if(userInput.isEmpty()) return;

    displayUserMessage(userInput);//append the user's new text to browser
    ui->UserInput->clear();
    ui->button_sendText->setEnabled(false);


    QJsonArray currentConversationHistory;
    //bool isNewConversation=false;
    if(currentConversationIndex!=-1&&currentConversationIndex<allConversations.size()){
        QJsonObject currentChatObject=allConversations.at(currentConversationIndex).toObject();
        currentConversationHistory=currentChatObject["dialogue"].toArray();
    }

    //check if it's new chat
    if(currentConversationIndex!=-1&&currentConversationIndex<allConversations.size()){
        //check pic gen
        if(ui->checkBox_imgGen->isChecked()){
            aiClient->sendPicGenerationRequest(userInput);
            ui->checkBox_imgGen->setEnabled(false);
        }else{
            //if it's a previous chat, send message here
            aiClient->sendMessage(userInput,currentConversationHistory);
        }
    }
    else{
        //only get title here, do not get reply from ai here!!!
        aiClient->requestConversationTitle(userInput);
    }
}
void MainWindow::on_button_newChat_clicked()
{
    if(!ui->button_sendText->isEnabled()){
        QMessageBox::information(this,"Info","Please wait the current chat is finished.");
        return;
    }
    currentConversationIndex=-1;
    ui->AIResponse->clear();
    chatHistoryList->selectionModel()->select(chatHistoryList->selectionModel()->currentIndex(),QItemSelectionModel::Deselect);
}


void MainWindow::closeEvent(QCloseEvent *event)
{
    if(!ui->button_sendText->isEnabled()){
        QMessageBox::warning(this,"Warning","Please wait the current chat is finished.");
        return;
    }
    event->ignore();
    this->hide(); //hide window
}
//Ai slots
void MainWindow::handleAiResponse(const QString &response)
{
    qDebug() << "AI Response Received: " << response; // Add this debug line to verify the response.
    displayAIMessage(response);
    ui->button_sendText->setEnabled(true);

    //sync the local json file
    if(currentConversationIndex!=-1&&currentConversationIndex<allConversations.size()){
        QJsonObject currentChat=allConversations.at(currentConversationIndex).toObject();
        QJsonArray chatArray=currentChat["dialogue"].toArray();

        QString lastUserMessage=userInput.trimmed();
        if(!chatArray.isEmpty()){
            QJsonObject lastEntry=chatArray.last().toObject();
            if(lastEntry.contains("user")&&lastEntry["user"].toString().trimmed()==lastUserMessage){
                //if the last msg was from user, update the ai response to file
                lastEntry["user"]=userInput;
                lastEntry["ai"]="Failed to get last reply";
                chatArray.append(lastEntry); //dev manual replies can be optimized
            }else{
                QJsonObject newEntry;
                newEntry["user"]=userInput;
                newEntry["ai"]=response;
                chatArray.append(newEntry);
            }
        }
        else{
            //this is a new chat
            QJsonObject newEntry;
            newEntry["user"]=userInput;
            newEntry["ai"]=response;
            chatArray.append(newEntry);
        }
        //update the chat
        currentChat["dialogue"]=chatArray;
        allConversations.replace(currentConversationIndex,currentChat);

        //update the data in model
        QString content;
        QStandardItem* item=chatHistoryModel->item(currentConversationIndex);
        QString color=getTextColorBasedOnTheme();
        for(const QJsonValue& value:chatArray){
            QString userMessage=value["user"].toString();
            QString aiMessage=value["ai"].toString();
            content += "<div style='text-align:right; color:"+color+";'><b>User:</b> " + userMessage + "</div><br>";
            content += "<div style='text-align:left; color:"+color+";'><b>AI:</b> " + aiMessage + "</div><br><br>";
        }
        item->setData(content,Qt::UserRole);
        ui->AIResponse->setHtml(content);
    }
    saveChatHistory();
}



void MainWindow::handleAiError(const QString &errorMsg)
{
    QMessageBox::critical(this,"Error",errorMsg);
    displayAIMessage(errorMsg);
    ui->button_sendText->setEnabled(true);
}

void MainWindow::handleTitleGenerated(const QString &title)
{
    QJsonObject newConversation;
    newConversation["title"]=title;
    newConversation["dialogue"]=QJsonArray();
    //dont have to append user msg in dialogue,handleAiResponse will append msg into the array

    allConversations.append(newConversation);
    currentConversationIndex=allConversations.size()-1;//update the index

    //update list view
    QStandardItem* item=new QStandardItem(title);
    item->setData(currentConversationIndex,Qt::UserRole+1);
    chatHistoryModel->appendRow(item);

    //make sure displays the new chat
    chatHistoryList->setCurrentIndex(chatHistoryModel->indexFromItem(item));
    ui->AIResponse->clear();
    displayUserMessage(userInput);

    //send message
    QJsonArray chatArray=allConversations.at(currentConversationIndex).toArray();
    if(ui->checkBox_imgGen->isChecked()){
        aiClient->sendPicGenerationRequest(userInput);
        ui->checkBox_imgGen->setEnabled(false);

    }else{
        aiClient->sendMessage(userInput,chatArray);
    }
    saveChatHistory();


}

void MainWindow::handleRewritedContent(const QString &rewritedContent)
{
    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setText(rewritedContent);//paste the rewritedContent to clipboard
    //debug
    QMessageBox::information(this, "Text Copied", "The rewritten text has been copied to the clipboard!");
    QTextToSpeech *speech = new QTextToSpeech();
    speech->say("rewrite complete");
}

void MainWindow::handlePicContent(const QPixmap &image)
{
    // 将 QPixmap 转换为 Base64 编码的字符串
    QByteArray byteArray;
    QBuffer buffer(&byteArray);
    buffer.open(QIODevice::WriteOnly);
    //save as png
    image.save(&buffer,"PNG");
    QString base64Image=byteArray.toBase64();

    QString textColor=getTextColorBasedOnTheme();

    QString htmlImg=QString("<div style='text-align:center; color:%1;'><b>Generated Image:</b></div><br>"
                              "<div style='text-align:center;'><img src='data:image/png;base64,%2' style='max-width:100%%; height:auto; border: 1px solid gray; border-radius: 8px;'/></div><br><br>")
                          .arg(textColor)
                          .arg(base64Image);
    // 将 HTML 添加到 QTextBrowser
    ui->AIResponse->append(htmlImg);

    ui->button_sendText->setEnabled(true);

    ui->checkBox_imgGen->setEnabled(true);
}
//slots end here




void MainWindow::on_comboBox_currentIndexChanged(int index)
{
    if(!ui->button_sendText->isEnabled()){
        QMessageBox::warning(this,"warning","Please wait the current chat is generated!");
        return;
    }
    delete aiClient;
    switch(static_cast<AI_Models>(index)){
    case AI_Models::ChatGPT:
        ChatGPTSetup();
        QMessageBox::information(this,"Info","Switched to ChatGPT");
        break;
    case AI_Models::Gemini:
        GeminiSetup();
        QMessageBox::information(this,"Info","Switched to GeminiClient");
        break;
    default:
        ChatGPTSetup();
        QMessageBox::information(this,"Info","Switched to ChatGPT(default)");
        break;
    }
}

