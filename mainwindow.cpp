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
#include <QLabel>
#include <QScrollArea>
#include <QScrollBar>
#include <QPalette>
#include <QSettings>
#include <QGroupBox>
#include "geminiclient.h"
#include "chatgptclient.h"
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow),clipboardMonitorTimer(nullptr)
{
    ui->setupUi(this);
    UISetup();
    loadQSS();
    setupEventFilter();
    ChatGPTSetup();
}

MainWindow::~MainWindow()
{
    saveChatHistory();
    delete popup;
    delete ui;
    conversationHistoriesVector.clear();
    if(clipboardMonitorTimer){
        clipboardMonitorTimer->stop(); // make sure the timer stopped
        delete clipboardMonitorTimer;
    }
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
        //stores the whole chat
        QVector<QPair<QString, QString>> currentConversationData;        //get individual conversations
        QJsonObject chatObject=value.toObject();
        //get title
        QString chatTitle=chatObject["title"].toString();
        QStandardItem* item=new QStandardItem(chatTitle);
        item->setData(i,Qt::UserRole+1);// save the index!!!

        model->appendRow(item); //add conversation title to list view

        //get dialog
        QJsonArray dialogArray=chatObject["dialogue"].toArray();
        for(const QJsonValue& dialogValue:dialogArray ){

            //add dialog to main page
            QString userMessage=dialogValue["user"].toString();
            QString AIMessage=dialogValue["ai"].toString();

            currentConversationData.push_back(qMakePair(userMessage,AIMessage));
            // >>> text QLabel output <<<
            qDebug() << "Loaded User Message (length " << userMessage.length() << "): " << userMessage.left(200) << (userMessage.length() > 200 ? "..." : "");
            qDebug() << "Loaded AI Message (length " << AIMessage.length() << "): " << AIMessage.left(200) << (AIMessage.length() > 200 ? "..." : "");
            item->setData(currentConversationData.size(),Qt::UserRole);
        }


        conversationHistoriesVector.push_back(currentConversationData);
        i++;
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

void MainWindow::loadQSS()
{
    QFile file(":/style/style.qss");  // make sure the path is correct
    if (file.open(QFile::ReadOnly)) {
        QString styleSheet = file.readAll();
        qApp->setStyleSheet(styleSheet);  // apply qss
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


// mainwindow.cpp

QWidget* MainWindow::createMessageBubble(const QString& message, bool isUserMessage)
{
    QWidget* bubbleContainer = new QWidget(this);
    bubbleContainer->setObjectName(isUserMessage ? "userMessageBubble" : "aiMessageBubble");

    QTextBrowser* textBrowser = new QTextBrowser();
    // 确保传入的是 HTML 格式的文本
    textBrowser->setHtml(plainTextToHtml(message));
    textBrowser->setReadOnly(true);
    textBrowser->setFrameShape(QFrame::NoFrame);
    textBrowser->setWordWrapMode(QTextOption::WordWrap);

    // **重要修正：确保滚动条不显示，并让高度自适应**
    textBrowser->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    textBrowser->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    // **关键修正：让 QTextBrowser 的大小策略为自适应**
    // 它的高度将由其内容决定，而不是被固定
    textBrowser->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);


    textBrowser->setMinimumWidth(100);
    //textBrowser->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);

    QTextDocument *doc = textBrowser->document();
    doc->setTextWidth(textBrowser->width());
    textBrowser->setMinimumHeight(doc->size().height());


    QVBoxLayout* layout = new QVBoxLayout(bubbleContainer);
    layout->addWidget(textBrowser);
    //layout->setContentsMargins(10, 10, 10, 10);


    return bubbleContainer;
}

QString MainWindow::plainTextToHtml(const QString &message)
{
    QString html = message;
    html.replace("\n", "<br>");
    // 示例：将 --- 替换为水平线
    html.replace("---", "<hr>");
    // 示例：将 # 转换为 <h1>
    html.replace("# ", "<h1>");
    // 你可以根据需要添加更多转换规则
    // ...
    return html;
}

void MainWindow::displayUserMessage(const QString &message)
{
    QWidget* userBubble=createMessageBubble(plainTextToHtml(message),true);
    vBoxLayout_ChatHistory->addWidget(userBubble, 0, Qt::AlignRight);
    //强制刷新
    updateScrollArea();
}

void MainWindow::displayAIMessage(const QString &message)
{
    QWidget* aiBubble=createMessageBubble(plainTextToHtml(message),false);
    vBoxLayout_ChatHistory->addWidget(aiBubble, 0, Qt::AlignLeft);
    //强制刷新
    updateScrollArea();
}

void MainWindow::displayAIMessage(const QPixmap &image)
{
    QLabel* imageLabel = new QLabel(this);

    // 计算一个最大宽度，例如聊天区域的一半
    int maxWidth = chatScrollArea->width() / 2;
    // 使用 scaled 函数在保持宽高比的情况下缩放图片
    QPixmap scaledPixmap = image.scaled(maxWidth, image.height(), Qt::KeepAspectRatio, Qt::SmoothTransformation);

    imageLabel->setPixmap(scaledPixmap);
    imageLabel->setFixedSize(scaledPixmap.size()); // 设置QLabel的固定大小为缩放后的图片大小

    QWidget* bubbleContainer = new QWidget(this);
    bubbleContainer->setObjectName("aiMessageBubble");
    QVBoxLayout* layout = new QVBoxLayout(bubbleContainer);
    layout->addWidget(imageLabel);
    layout->setAlignment(Qt::AlignLeft);
    layout->setContentsMargins(5, 5, 5, 5);

    vBoxLayout_ChatHistory->addWidget(bubbleContainer, 0, Qt::AlignLeft);
    updateScrollArea();
}


void MainWindow::deleteVBoxChildren()
{
    QLayoutItem* item;
    while ((item = vBoxLayout_ChatHistory->takeAt(0)) != nullptr) {
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }
}

//rewrite function related
void MainWindow::startClipboardMonitoring()
{
    if (!clipboardMonitorTimer) { // Make sure only one timer object
        clipboardMonitorTimer = new QTimer(this);
        connect(clipboardMonitorTimer, &QTimer::timeout, this, &MainWindow::checkClipboard);
    }
    if(!clipboardMonitorTimer->isActive()){
        clipboardMonitorTimer->start(1000); // check clipboard every 1s
        qDebug() << "Clipboard monitoring started.";
    }
}

void MainWindow::stopClipboardMonitoring()
{
    if(clipboardMonitorTimer&&clipboardMonitorTimer->isActive()){
        clipboardMonitorTimer->stop();
        qDebug() << "Clipboard monitoring stopped.";
    }
}

void MainWindow::showRewritePrompt(const QString &copiedText)
{
    QPoint mousePosition=QCursor::pos();//get current mouse position
    //popup the widget offer rewrite choice
    popup=new QWidget();
    popup->setWindowFlags(Qt::ToolTip|Qt::FramelessWindowHint);
    popup->setAttribute(Qt::WA_TranslucentBackground);

    // Set the window background color using QPalette::Window
    QPalette palette = popup->palette();
    palette.setColor(QPalette::Window, QColor(240, 244, 248));  // set winddow background color
    popup->setAutoFillBackground(true);
    popup->setPalette(palette);

    // Create a vertical layout for the popup window
    QVBoxLayout* popupLayout = new QVBoxLayout(popup);
    popupLayout->setContentsMargins(5, 5, 5, 5); // Set the layout's padding to give the button a small space from the popup edge.
    popupLayout->setSpacing(2); // Set the spacing between buttons to make them more compact

    QPushButton* rewriteButton=new QPushButton("Rewrite",popup);
    QPushButton* rewriteButtonWithPrompt=new QPushButton("Rewrite with prompt",popup);
    popupLayout->addWidget(rewriteButton);
    popupLayout->addWidget(rewriteButtonWithPrompt);
    rewriteButton->setFixedHeight(30);
    rewriteButtonWithPrompt->setFixedHeight(30);
    QString buttonStyle = R"(
        QPushButton {
            background-color: #f0f0f0;
            color: #333333;
            border: 1px solid #cccccc;
            border-radius: 5px;
            padding: 5px 10px;
            font-size: 13px;
            font-weight: normal;
            icon-size: 18px 18px;
        }
        QPushButton:hover {
            background-color: #e0e0e0;
            border-color: #a0a0a0;
        }
        QPushButton:pressed {
            background-color: #d0d0d0;
        }
    )";
    // apply style to buttons
    rewriteButton->setStyleSheet(buttonStyle);
    rewriteButtonWithPrompt->setStyleSheet(buttonStyle);
    rewriteButton->setIcon(QIcon(":/icons/icons/magicwand.png"));
    rewriteButtonWithPrompt->setIcon(QIcon(":/icons/icons/magicwand.png"));
    // Make the popup window automatically resize according to the content of its layout
    popup->adjustSize();
    // After resizing, move the popup window to the mouse position
    popup->move(mousePosition.x(), mousePosition.y());
    connect(rewriteButton,&QPushButton::clicked,this,&MainWindow::rewriteText);
    connect(rewriteButtonWithPrompt,&QPushButton::clicked,this,&MainWindow::onRewriteWithPromptClicked);

    //hover 3s then close automatically
    QTimer::singleShot(3000, popup, &QWidget::close);
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

    this->setWindowFlags(Qt::FramelessWindowHint | Qt::WindowMinimizeButtonHint | Qt::WindowCloseButtonHint | Qt::WindowSystemMenuHint);
    // Set transparent background
    this->setAttribute(Qt::WA_TranslucentBackground);

    // Set the window background color
    QPalette palette = this->palette();
    palette.setColor(QPalette::Window, QColor(240, 244, 248));  // Set the window background color to light blue
    this->setAutoFillBackground(true);
    this->setPalette(palette);



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
    chatHistoryList->setSpacing(10);
    // QVBoxLayout* vBoxLayout_listview=new QVBoxLayout(this);
    // vBoxLayout_listview->addWidget(chatHistoryList, Qt::AlignTop | Qt::AlignLeft);
    // vBoxLayout_listview->setContentsMargins(15, 15, 15, 15);
    // vBoxLayout_listview->setSpacing(5);
    // vBoxLayout_ChatHistory->addStretch();
    chatHistoryList->setMinimumSize(280, 400); // Set the minimum width to 300 and the height to 400

    //Create a new VBox to hold the dialog label
    ui->groupBox_functionalArea->setStyleSheet("QGroupBox { border: none; }");
    ui->groupBox_chatBox->setStyleSheet("QGroupBox { border: none; }");

    vBoxLayout_ChatHistory=new QVBoxLayout();
    vBoxLayout_ChatHistory->setAlignment(Qt::AlignTop);
    //vBoxLayout_ChatHistory->addSpacing(5);

    //Create a QWidget to hold the message and set the QVBoxLayout to it
    QWidget* messagesContainerWidget = new QWidget();
    messagesContainerWidget->setLayout(vBoxLayout_ChatHistory);

    //Creating the scrollarea
    chatScrollArea=new QScrollArea();
    chatScrollArea->setWidgetResizable(true);
    chatScrollArea->setWidget(messagesContainerWidget);
    chatScrollArea->setFrameShape(QFrame::NoFrame);

    QLayout* chatBoxLayout = ui->groupBox_chatBox->layout();
    if (!chatBoxLayout) {
        chatBoxLayout = new QVBoxLayout(ui->groupBox_chatBox);
    }
    chatBoxLayout->addWidget(chatScrollArea);
    //chatBoxLayout->setContentsMargins(0, 0, 0, 0);

    // QSplitter
    QSplitter* splitter = new QSplitter(Qt::Horizontal);
    splitter->addWidget(chatHistoryList); // Add chat history list to splitter
    //right pane
    QWidget* rightPaneWidget = new QWidget();
    QVBoxLayout* rightPaneLayout = new QVBoxLayout(rightPaneWidget);
    rightPaneLayout->addWidget(ui->groupBox_chatBox);
    rightPaneLayout->addWidget(ui->groupBox_functionalArea);
    rightPaneLayout->setContentsMargins(0, 0, 0, 0);

    splitter->addWidget(rightPaneWidget);// Adding a chat area to splitter
    setCentralWidget(splitter);
    loadChatHistory(chatHistoryModel); //load chat history to listview

    //create a closebutton and a hidebutton
    closeButton = new QPushButton(this);
    closeButton->setFixedSize(30, 30);
    closeButton->setIcon(QIcon(":/icons/icons/close.png"));
    closeButton->setStyleSheet(
        "QPushButton {"
        "   background-color: #F0F4F8;"
        "   color: white;"
        "   border-radius: 15px;"
        "   font-weight: bold;"
        "   border: none;"
        "   text-align: center;"
        "   icon-size: 20px 20px; "
        "}"
        "QPushButton:hover {"
        "   background-color: #e60000;"
        "}"
        );
    connect(closeButton, &QPushButton::clicked, this, &QWidget::close);

    //create a hidebutton
    hideButton = new QPushButton(this);
    hideButton->setFixedSize(30, 30);
    hideButton->setIcon(QIcon(":/icons/icons/hide.png"));
    hideButton->setStyleSheet(
        "QPushButton {"
        "   background-color: #F0F4F8;"
        "   color: white;"
        "   border-radius: 15px;"
        "   font-weight: bold;"
        "   border: none;"
        "   text-align: center;"
        "   icon-size: 20px 20px; "
        "}"
        "QPushButton:hover {"
        "   background-color: #0056b3;"
        "}"
        );
    connect(hideButton, &QPushButton::clicked, this, &QWidget::hide);

    //show conversation
    connect(chatHistoryList,&QListView::clicked,this,&MainWindow::do_showChatHistory);
    connect(chatHistoryList,&QListView::doubleClicked,this,&MainWindow::do_showChatHistory);

}

void MainWindow::GeminiSetup()
{
    //initialized Gemini client
    aiClient = new GeminiClient(this);
    QSettings settings("config.ini", QSettings::IniFormat);
    QString apiKey = settings.value("API/GeminiKey").toString();
    if (apiKey.isEmpty() || apiKey == "YOUR_ACTUAL_GEMINI_API_KEY_HERE") {
        // If Key is empty or still a placeholder, prompt the user
        QMessageBox::warning(this, "API Key Missing",
                             "Gemini API Key is not set or is invalid in config.ini.\n"
                             "Please open 'config.ini' in the application directory and replace 'YOUR_ACTUAL_GEMINI_API_KEY_HERE' with your actual API Key.");
        // To avoid program crashes, disabled related features or exit
        ui->button_sendText->setEnabled(false); // Disable the Send Button
        return;
    } else {
        aiClient->setApiKey(apiKey);
        ui->button_sendText->setEnabled(true);
        qDebug() << "Gemini API Key loaded from config.ini.";
    }
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
    QSettings settings("config.ini", QSettings::IniFormat);
    QString apiKey = settings.value("API/ChatGPTKey").toString();
    if (apiKey.isEmpty() || apiKey == "YOUR_ACTUAL_ChatGPT_API_KEY_HERE") {
        QMessageBox::warning(this, "API Key Missing",
                             "ChatGPT API Key is not set or is invalid in config.ini.\n"
                             "Please open 'config.ini' in the application directory and replace 'YOUR_ACTUAL_ChatGPT_API_KEY_HERE' with your actual API Key.");
        ui->button_sendText->setEnabled(false);
        return;
    } else {
        aiClient->setApiKey(apiKey);
        ui->button_sendText->setEnabled(true);
        qDebug() << "ChatGPT API Key loaded from config.ini.";
    }
    connect(aiClient,&ChatGPTClient::generatedImgReceived,this,&MainWindow::handlePicContent);
    connect(aiClient,&ChatGPTClient::aiResponseReceived,this,&MainWindow::handleAiResponse);
    connect(aiClient,&ChatGPTClient::titleGenerated,this,&MainWindow::handleTitleGenerated);
    connect(aiClient,&ChatGPTClient::errorOccured,this,&MainWindow::handleAiError);
    connect(aiClient,&ChatGPTClient::rewritedContentReceived,this,&MainWindow::handleRewritedContent);
}

//user can press enter to send text
void MainWindow::setupEventFilter()
{
    ui->UserInput->installEventFilter(this);  // Install an event filter for the user input box
}
// 强制更新布局
void MainWindow::updateScrollArea()
{
    QCoreApplication::processEvents();
    QScrollBar *scrollBar = chatScrollArea->verticalScrollBar();
    scrollBar->setValue(scrollBar->maximum());
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if(watched==ui->UserInput){
        if(event->type()==QEvent::KeyPress){
            QKeyEvent* keyEvent=static_cast<QKeyEvent*>(event);
            if(keyEvent->key()==Qt::Key_Enter|| keyEvent->key() == Qt::Key_Return){
                if (keyEvent->modifiers() & Qt::ShiftModifier) {
                    // If Shift+Enter is pressed, line break is allowed
                    return false;  // Return false, the event continues to be passed to QPlainTextEdit for line wrapping
                } else {
                    // Otherwise, trigger a button click
                    ui->button_sendText->click();
                    return true;  // Return true, indicating that the event has been processed
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
    //get current conversation index
    currentConversationIndex=chatHistoryList->selectionModel()->currentIndex().row();
    deleteVBoxChildren();
    if (currentConversationIndex >= 0 && currentConversationIndex < conversationHistoriesVector.size()) {
        QVector<QPair<QString,QString>> currentConversationDisplay = conversationHistoriesVector.at(currentConversationIndex);
        for(const QPair<QString, QString>& dialogPair  : currentConversationDisplay){
            displayUserMessage(dialogPair.first);
            QString aiResponse=dialogPair.second;
            if(aiResponse.startsWith("data:image")){
                QString base64Image=aiResponse.mid(aiResponse.indexOf(',') + 1);
                QByteArray data=QByteArray::fromBase64(base64Image.toUtf8());
                QPixmap pixmap;
                if(pixmap.loadFromData(data)){
                    displayAIMessage(pixmap);
                }else{
                    displayAIMessage("Failed to load image from history.");
                }
            }else{
                displayAIMessage(aiResponse);
            }


        }
    }
    updateScrollArea();
}

void MainWindow::on_button_sendText_clicked()
{
    userInput=ui->UserInput->toPlainText().trimmed();//save the user input
    if(userInput.isEmpty()) return;

    displayUserMessage(userInput);//append the user's new text to browser
    // After sending the message, let the scrollArea scroll to the bottom >>(Not working)<<
    updateScrollArea();
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
    deleteVBoxChildren();//Delete previous widgets
    chatHistoryList->selectionModel()->clearSelection();
}


void MainWindow::closeEvent(QCloseEvent *event)
{
    if(!ui->button_sendText->isEnabled()){
        QMessageBox::warning(this,"Warning","Please wait the current chat is finished.");
        event->ignore();
        return;
    }
    saveChatHistory();
    this->hide(); //hide window
}
//Ai slots
void MainWindow::handleAiResponse(const QString &response)
{
    qDebug() << "AI Response Received: " << response; // Add this debug line to verify the response.
    displayAIMessage(response);
    // scroll to buttom(not working)
    updateScrollArea();


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
        QJsonObject lastEntry = chatArray[chatArray.size()-1].toObject();

        // Get the "user" and "ai" fields
        QString userMsg = lastEntry["user"].toString();
        QString aiMsg = lastEntry["ai"].toString();

        QStandardItem* item=chatHistoryModel->item(currentConversationIndex);
        currentConversationIndex=chatHistoryList->selectionModel()->currentIndex().row();
        QVector<QPair<QString,QString>> currentConversationHistory=conversationHistoriesVector.at(currentConversationIndex);
        if(currentConversationIndex!=-1&&currentConversationIndex < conversationHistoriesVector.size()){
            conversationHistoriesVector[currentConversationIndex].append(qMakePair(userMsg,aiMsg));
        } else {
            QVector<QPair<QString,QString>> newChatHistory;
            newChatHistory.append(qMakePair(userMsg,aiMsg));
            conversationHistoriesVector.append(newChatHistory);
        }
        //debug
        int size=conversationHistoriesVector.at(conversationHistoriesVector.size()-1).size();
        QPair<QString,QString> lastChat=conversationHistoriesVector.at(conversationHistoriesVector.size()-1).at(size-1);
        item->setData(currentConversationHistory.size(),Qt::UserRole);
    }
    ui->button_sendText->setEnabled(true);
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
    // Initialize conversationHistoriesVector for this new conversation and add an empty QVector<QPair<QString, QString>>
    conversationHistoriesVector.append(QVector<QPair<QString, QString>>());

    //send message
    QJsonObject currentChatObject = allConversations.at(currentConversationIndex).toObject();
    QJsonArray chatArray = currentChatObject["dialogue"].toArray();
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
    QApplication::beep();//notify user task has done
}

void MainWindow::handlePicContent(const QPixmap &image)
{
    displayAIMessage(image);
    QByteArray byteArray;
    QBuffer buffer(&byteArray);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer,"PNG");
    QString base64Img=QString("data:image/png;base64,")+QString::fromUtf8(byteArray.toBase64());
    if (currentConversationIndex != -1 && currentConversationIndex < allConversations.size()){
        QJsonObject currentChat = allConversations.at(currentConversationIndex).toObject();
        QJsonArray dialogArray=currentChat["dialogue"].toArray();
        QJsonObject newEntry;
        newEntry["user"] = userInput;
        newEntry["ai"] = base64Img;
        dialogArray.append(newEntry);
        currentChat["dialogue"] = dialogArray;
        allConversations.replace(currentConversationIndex,currentChat);
    }
    // Ensure the scroll area updates to show the new image
    updateScrollArea();

    // Re-enable the send button and image generation checkbox
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

//virtual functions
void MainWindow::mousePressEvent(QMouseEvent *event)
{
    if( (event->button()== Qt::LeftButton)){
        mouse_press = true;
        mousePoint = event->globalPos() - this->pos();
        event->accept();
    }
    else if(event->button() == Qt::RightButton){
        this->close();
    }

}

void MainWindow::mouseReleaseEvent(QMouseEvent *event)
{
    mouse_press=false;
}

void MainWindow::mouseMoveEvent(QMouseEvent *event)
{
    if(event->buttons() == Qt::LeftButton){
        if(mouse_press){
            move(event->globalPos() - mousePoint);
            event->accept();
        }
    }
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event); //Call the base class implementation

    if (closeButton && hideButton) { // Make sure both buttons are present
        int margin = 10; // Margin from the edge of the window
        int buttonSpacing = 5; // The distance between two buttons

        // 1. closeButton
        int closeButtonX = this->width() - closeButton->width() - margin;
        int buttonY = margin;

        closeButton->move(closeButtonX, buttonY);

        // 2. hideButton
        int hideButtonX = closeButtonX - hideButton->width() - buttonSpacing;
        hideButton->move(hideButtonX, buttonY);
    }

    // 如果聊天历史列表有选中项，则重新显示聊天记录以调整图片大小
    if (chatHistoryList && chatHistoryList->selectionModel()->hasSelection()) {
        QModelIndex selectedIndex = chatHistoryList->selectionModel()->currentIndex();
        do_showChatHistory(selectedIndex);
    }

}
