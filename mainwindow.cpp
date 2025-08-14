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
#include <vector>
#include "geminiclient.h"
#include "chatgptclient.h"
#include <atomic>

std::atomic_bool g_ignoreHook{false};       // 粘贴时临时忽略钩子
std::atomic_bool g_captureInFlight{false};  // 有抓取在途时不再排队
//===========windows api
//initialize
HHOOK g_hKeyboardHook = NULL;
IUIAutomation* g_pAutomation = NULL;
MainWindow* g_pMainWindow = NULL;
//global hook  callback
LRESULT CALLBACK LowLevelKeyboardProc(int nCode,WPARAM wParam,LPARAM lParam){
    //WPARAM 键盘事件的类型
    //LPARAM 包含与键盘事件相关的附加信息
    //nCode负值（表示消息未被处理）或零及正值（表示消息已被处理）
    if(nCode<0){
        return CallNextHookEx(g_hKeyboardHook,nCode,wParam,lParam);
    }
    // 任何时候，如果正在模拟输入（Ctrl+A/V）就直接放行
    if(g_ignoreHook.load(std::memory_order_relaxed)){
        return CallNextHookEx(g_hKeyboardHook,nCode,wParam,lParam);
    }

    if(wParam==WM_KEYUP||wParam==WM_SYSKEYUP){
        KBDLLHOOKSTRUCT* pKbdStruct = (KBDLLHOOKSTRUCT*)(lParam);
        // ====== 1. 获取焦点控件的句柄 ======
        HWND hWndFocus=NULL;
        GUITHREADINFO guiInfo={0};
        guiInfo.cbSize=sizeof(GUITHREADINFO);
        if(GetGUIThreadInfo(NULL,&guiInfo)){// NULL表示获取前景线程的信息
            hWndFocus=guiInfo.hwndFocus;// 焦点控件的句柄
        }

        if(hWndFocus!=NULL && g_pMainWindow!=NULL && g_pAutomation!=NULL){
            // 若已有抓取在途，则丢弃本次（节流）
            bool already=g_captureInFlight.exchange(true,std::memory_order_acq_rel);
            if(!already){
                HWND h = hWndFocus;
                // ====== 2. 使用 UI Automation 获取文本 ======
                QMetaObject::invokeMethod(g_pMainWindow,[h](){
                    g_pMainWindow->getControlTextUIAInternal(h);
                },Qt::QueuedConnection);
            }

        }
    }
    // 将消息传递给链中的下一个钩子
    return CallNextHookEx(g_hKeyboardHook,nCode,wParam,lParam);
}
//===================
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow),clipboardMonitorTimer(nullptr)
{
    ui->setupUi(this);
    UISetup();
    loadQSS();
    setupEventFilter();
    ChatGPTSetup();

    //install hook, innitialized UIA and COM
    g_pMainWindow=this;
    initializeUIA();
    if(!installGlobalKeyboardHook()){
        QMessageBox::critical(this,"Error","Can't install global keyboard hook!");
    }
    isClipboardEvent=false;//默认不开启

    typingTimer=new QTimer(this);
    typingTimer->setSingleShot(true);
    connect(typingTimer,&QTimer::timeout,this,&MainWindow::onTypingPause);

    clipboardMonitorTimer=new QTimer(this);
    connect(clipboardMonitorTimer,&QTimer::timeout,this,&MainWindow::checkClipboard);
}

MainWindow::~MainWindow()
{
    saveChatHistory();
    delete ui;
    conversationHistoriesVector.clear();
    if(typingTimer){
        typingTimer->stop();;
        delete typingTimer;
    }
    if(clipboardMonitorTimer){
        clipboardMonitorTimer->stop(); // make sure the timer stopped
        delete clipboardMonitorTimer;
    }
    //clear hook and UIA
    uninstallGlobalKeyboardHook();
    uninitializeUIA();
    if (editBlock) { editBlock->Release(); editBlock = nullptr; }

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
    isClipboardMonitorEnabled=true;
    isClipboardEvent=true;

    //进入复制模式前先把旧 UI 和旧文本清掉
    hideRewriteUI();
    pendingTextForRewrite.clear();
    lastClipboardText.clear();
    previousClipboardText.clear();

    if(clipboardMonitorTimer){
        clipboardMonitorTimer->start(1000);
        qDebug() << "Clipboard monitoring started.";
    }
}

void MainWindow::stopClipboardMonitoring()
{
    isClipboardMonitorEnabled = false;
    isClipboardEvent=false;
    if(clipboardMonitorTimer&&clipboardMonitorTimer->isActive()){
        clipboardMonitorTimer->stop();
        qDebug() << "Clipboard monitoring stopped.";
    }

    // 新增：退出复制模式时复位 UI 和文本
    hideRewriteUI();
    pendingTextForRewrite.clear();
    lastClipboardText.clear();
    previousClipboardText.clear();
    lastCapturedText.clear();
}

//=======windows api
void MainWindow:: initializeUIA()
{
    qDebug() << "Initializing COM and UIA...";
    // 只需要初始化COM一次
    HRESULT hr=CoInitializeEx(NULL,COINIT_APARTMENTTHREADED);//用于初始化 COM 库。它设置当前线程的 COM 运行环境，允许该线程使用 COM 功能。COINIT_APARTMENTTHREADED: 指定线程的并发模型为单元线程（STA）。
    if(FAILED(hr)&&hr!=S_FALSE){
        qWarning() << "Failed to initialize COM library. HRESULT:" << hr;
        return;
    }
    qDebug() << "COM initialized successfully.";
    // 创建IUIAutomation对象
    if(g_pAutomation==NULL){
        hr=CoCreateInstance(CLSID_CUIAutomation,//CLSID_CUIAutomation: 指定要创建的对象的类标识符（CLSID），表示 UI 自动化对象。
                              NULL,//表示没有保留的参数。
                              CLSCTX_INPROC_SERVER,//指定对象将在同一进程中创建。
                              IID_IUIAutomation,//指定请求的接口标识符（IID），表示希望获得 IUIAutomation 接口。
                              (void**)&g_pAutomation);//将创建的对象指针存储到 g_pAutomation 中
        if(FAILED(hr)){
            qWarning() << "Failed to create IUIAutomation object. HRESULT:" << hr;
            g_pAutomation = NULL; // 确保失败时为NULL
            return;
        }
        qDebug() << "IUIAutomation object created successfully. Pointer:" << g_pAutomation;
    }else{
        qDebug() << "IUIAutomation object already exists.";
    }
}

void MainWindow::uninitializeUIA()
{
    qDebug() << "Uninitializing COM and UIA...";
    if(g_pAutomation){
        g_pAutomation->Release();//release UIA object
        g_pAutomation=NULL;
        qDebug() << "IUIAutomation object released.";
    }
    CoUninitialize();
    qDebug() << "COM uninitialized.";
}

bool MainWindow::installGlobalKeyboardHook()
{
    //install global low level keyboard hook
    // GetModuleHandle(NULL) 获取当前进程的模块句柄
    g_hKeyboardHook=SetWindowsHookEx(WH_KEYBOARD_LL,LowLevelKeyboardProc,GetModuleHandle(NULL),0);
    if(g_hKeyboardHook==NULL){
        qCritical() << "Failed to install global keyboard hook! Error:" << GetLastError();
        return false;
    }
    qDebug() << "Global keyboard hook installed successfully.";
    return true;
}

void MainWindow::uninstallGlobalKeyboardHook()
{
    if(g_hKeyboardHook!=NULL){
        UnhookWindowsHookEx(g_hKeyboardHook);
        g_hKeyboardHook=NULL;
        qDebug() << "Global keyboard hook uninstalled.";
    }
}
IUIAutomationElement *MainWindow::findChildEditControl(IUIAutomationElement *parentElement)
{
    if(parentElement==NULL){
        return NULL;
    }


    // 1. 创建一个 VARIANT 结构体
    VARIANT vtControlType; VariantInit(&vtControlType);
    // 2. 将其类型设置为 VT_I4 (表示一个32位有符号整数)
    vtControlType.vt = VT_I4;
    // 创建条件，用于查找“编辑框”控件
    IUIAutomationCondition* cEdit = nullptr;
    IUIAutomationCondition* cDoc  = nullptr;
    IUIAutomationCondition* cText = nullptr;
    IUIAutomationCondition* cOr1  = nullptr;
    IUIAutomationCondition* cAny  = nullptr;

    // 3. 将 UIA_EditControlTypeId 的值赋给它
    vtControlType.lVal = UIA_EditControlTypeId;
    if(FAILED(g_pAutomation->CreatePropertyCondition(UIA_ControlTypePropertyId,vtControlType,&cEdit))) return nullptr;
    vtControlType.lVal=UIA_DocumentControlTypeId;
    if(FAILED(g_pAutomation->CreatePropertyCondition(UIA_ControlTypePropertyId,vtControlType,&cDoc))) return nullptr;
    vtControlType.lVal=UIA_TextControlTypeId;
    if(FAILED(g_pAutomation->CreatePropertyCondition(UIA_ControlTypePropertyId,vtControlType,&cText))) return nullptr;

    // (Edit OR Document) OR Text
    if(FAILED(g_pAutomation->CreateOrCondition(cEdit,cDoc,&cOr1))) {cEdit->Release(); cDoc->Release(); cText->Release(); return nullptr;}
    if(FAILED(g_pAutomation->CreateOrCondition(cOr1,cText,&cAny))) {cEdit->Release(); cDoc->Release(); cText->Release(); cOr1->Release(); return nullptr;}


    IUIAutomationElementArray* pFoundElements=NULL;
    HRESULT hr=parentElement->FindAll(TreeScope_Subtree,cAny,&pFoundElements);// 遍历整个子元素树
    cEdit->Release(); cDoc->Release(); cText->Release(); cOr1->Release(); cAny->Release();
    if (FAILED(hr) || !pFoundElements) return nullptr;

    int count=0;
    pFoundElements->get_Length(&count);
    if (count > 0){
        IUIAutomationElement * foundElement;
        pFoundElements->GetElement(0,&foundElement);
        pFoundElements->Release();
        return foundElement;
    }
    pFoundElements->Release();
    return nullptr;
    //// 查找符合条件的子元素
}

void MainWindow::setControlTextUIA(IUIAutomationElement *element, const QString &text)
{
    if(!element){
        qWarning("setControlTextUIA: IUIAutomationElement is null.");
        pasteTextIntoActiveControl(text);
        return;
    }
    BSTR bstrText=SysAllocString(reinterpret_cast<const OLECHAR*>(text.utf16()));
    IUIAutomationValuePattern* pValuePattern=NULL;
    HRESULT hr=element->GetCurrentPatternAs(UIA_ValuePatternId,IID_IUIAutomationValuePattern,(void**)&pValuePattern);
    if(SUCCEEDED(hr)&&(pValuePattern!=NULL)){
        hr=pValuePattern->SetValue(bstrText);
        pValuePattern->Release();
        SysFreeString(bstrText);    // 释放 BSTR
        if (SUCCEEDED(hr)) return;
        qWarning("setControlTextUIA: SetValue failed, fallback to paste.");
    }
    else{
        SysFreeString(bstrText);    // 释放 BSTR
        qWarning("setControlTextUIA: ValuePattern not supported, fallback to paste.");
    }
    pasteTextIntoActiveControl(text);
}

void MainWindow::pasteTextIntoActiveControl(const QString &text)
{
    g_ignoreHook.store(true, std::memory_order_release);  // ← 开闸：忽略假按键
    QClipboard* clipboard=QApplication::clipboard();
    clipboard->setText(text);
    HWND hwndFocus=GetFocus();
    if(hwndFocus==NULL){
        g_ignoreHook.store(false, std::memory_order_release);  // 重要：复位
        return;
    }
    SetFocus(hwndFocus);//设置焦点
    Sleep(50);// 稍作等待以确保焦点切换完成

    // 模拟 Ctrl + A (全选)
    keybd_event(VK_CONTROL,0x90,0,0);
    keybd_event(0x41,0x9D, 0, 0);
    keybd_event(0x41, 0x9D, KEYEVENTF_KEYUP,0);// 抬起'A'
    keybd_event(VK_CONTROL, 0x9D, KEYEVENTF_KEYUP, 0);// 抬起Ctrl

    //模拟 Ctrl + V (粘贴)
    keybd_event(VK_CONTROL,0x90,0,0);
    keybd_event(0x56,0x9D, 0, 0);
    keybd_event(0x56, 0x9D, KEYEVENTF_KEYUP,0);// 抬起'v'
    keybd_event(VK_CONTROL, 0x9D, KEYEVENTF_KEYUP, 0);// 抬起Ctrl
    // 150ms 后恢复钩子
    QTimer::singleShot(150, this, [](){
        g_ignoreHook.store(false, std::memory_order_release);
    });
}
void MainWindow::getControlTextUIAInternal(HWND hwndFocus) {
    //确保无论如何都清掉在途标志
    struct Guard { ~Guard(){ g_captureInFlight.store(false, std::memory_order_release); } } _g;
    if (!g_pAutomation) { qWarning() << "UIA automation object is not initialized."; return; }

    QString extractedText;//是 UI Automation 中的一个接口，用于表示和操作 UI 元素
    BSTR bstrText;//是一种用于表示字符串的 COM 类型，支持宽字符和内存管理
    IUIAutomationElement* pElement;

    pElement=getFocusedUIAElement();
    if(!pElement){
        // 兜底：从句柄拿到顶层元素
        HRESULT hr=g_pAutomation->ElementFromHandle(hwndFocus,&pElement);
        if(FAILED(hr)||pElement==NULL){
            qDebug() << "Failed to get UIA element from handle. HRESULT:" << hr;
            return;
        }
    }

    VARIANT v{};
    bool hasValue=false; bool hasText=false;
    if(SUCCEEDED(pElement->GetCurrentPropertyValue(UIA_IsValuePatternAvailablePropertyId,&v))){
        hasValue=(v.vt==VT_BOOL&&v.boolVal==VARIANT_TRUE);
        VariantClear(&v);
    }
    if(SUCCEEDED(pElement->GetCurrentPropertyValue(UIA_IsTextPatternAvailablePropertyId,&v))){
        hasText =hasText ||(v.vt==VT_BOOL&&v.boolVal==VARIANT_TRUE);
        VariantClear(&v);
    }
    if(SUCCEEDED(pElement->GetCurrentPropertyValue(UIA_IsTextEditPatternAvailablePropertyId,&v))){
        hasText =hasText ||(v.vt==VT_BOOL&&v.boolVal==VARIANT_TRUE);
        VariantClear(&v);
    }

    IUIAutomationElement* pInputControl=nullptr;
    if(hasValue||hasText){
        pInputControl=pElement;// 直接用 focused 节点
        pInputControl->AddRef();
    }else{
        pInputControl=findChildEditControl(pElement);
        if(pInputControl==NULL){
            qDebug() << "Did not find a child edit control, using parent element.";
            pInputControl = pElement;//if is null, use parent element
            pInputControl->AddRef();//pInputControl如果用原来的 增加引用计数
        }
        else{
            qDebug() << "Found a child edit control.";
        }
    }

    // 找子编辑控件，找不到就用父元素
    IUIAutomationElement* old = editBlock;
    editBlock = pInputControl;
    editBlock->AddRef();
    if (old) { old->Release(); old = nullptr; }

    // 获取输入框的屏幕坐标
    RECT r{}; HRESULT hr = pInputControl->get_CurrentBoundingRectangle(&r);
    QRect boundingRect;
    if (SUCCEEDED(hr)) {
        boundingRect = QRect(r.left, r.top, r.right - r.left, r.bottom - r.top);
    } // 失败就留空 rect（无效），稍后 processCapturedText 会按光标定位

    // 尝试获取 ValuePattern (适用于大部分简单的文本框，如网页输入框、记事本)
    //UI Automation（UIA）中的一个模式，用于表示和操作具有可编辑值的 UI 元素。
    IUIAutomationValuePattern* pValuePattern=NULL;
    hr=pInputControl->GetCurrentPatternAs(UIA_ValuePatternId,IID_IUIAutomationValuePattern,(void**)&pValuePattern);
    if(SUCCEEDED(hr)&&pValuePattern!=NULL){
        hr=pValuePattern->get_CurrentValue(&bstrText);
        if(SUCCEEDED(hr)&&bstrText!=NULL){
            extractedText =QString::fromWCharArray(reinterpret_cast<const wchar_t*>(bstrText));
        }
        if(bstrText){SysFreeString(bstrText);bstrText=NULL;}
        pValuePattern->Release();

    }
    if(extractedText.isEmpty()){
        //if valuePatter is not working, try textpattern
        // 如果 ValuePattern 不行，尝试获取 TextPattern (适用于富文本、复杂编辑器，如 Word, QQ聊天框)
        IUIAutomationTextPattern* pTextPattern=NULL;
        hr=pInputControl->GetCurrentPatternAs(UIA_TextPatternId,IID_IUIAutomationTextPattern,(void**)&pTextPattern);
        if(SUCCEEDED(hr)&&pTextPattern != NULL){
            IUIAutomationTextRange* pTextRange=NULL;
            hr=pTextPattern->get_DocumentRange(&pTextRange);
            if(SUCCEEDED(hr)&&pTextRange!=NULL){
                hr=pTextRange->GetText(-1,&bstrText);//-1 means extract all text
                if(SUCCEEDED(hr)&&bstrText!=NULL){
                    extractedText =QString::fromWCharArray(reinterpret_cast<const wchar_t*>(bstrText));
                }
                if(bstrText){SysFreeString(bstrText);bstrText=NULL;}
                pTextRange->Release();
            }
            pTextPattern->Release();
        }
    }

    pInputControl->Release(); pInputControl = nullptr;
    pElement->Release(); pElement = nullptr;

    //将文本和坐标一起传递回主线程
    if(!extractedText.isEmpty()){
        QMetaObject::invokeMethod(g_pMainWindow,"processCapturedText",
                                  Qt::QueuedConnection,
                                  Q_ARG(QString,extractedText),
                                  Q_ARG(QRect,boundingRect));
    }
}
//public slot
void MainWindow::processCapturedText(const QString &text,const QRect& rect)
{
    if (text.isEmpty() || text == lastCapturedText) {
        return;
    }
    // 先更新缓存，保证按钮随时能发“最新文本”
    lastCapturedRect = rect;
    lastCapturedText = text;
    pendingTextForRewrite = text;
    // 面板已在显示：只更新数据，不重复弹
    if(getIsRewriteFlowActive()){
        return;
    }
    // 未显示：照旧开启防抖，稍后 show
    qDebug() << "Captured text:" << text;
    typingTimer->start(800);
}

void MainWindow::onTypingPause()
{
    if(lastCapturedText.isEmpty()){
        return;
    }
    //showRewritePrompt(lastCapturedText);
    showRewriteUI(RewriteUIPlacement::AnchorToRect,lastCapturedRect,lastCapturedText);
}
//==============end windows api

void MainWindow::ensureRewritePanelInit()
{
    if (rewritePanel) return;
    rewritePanel=new QWidget();
    rewritePanel->setWindowFlags(Qt::ToolTip|Qt::FramelessWindowHint);
    rewritePanel->setAttribute(Qt::WA_TranslucentBackground);
    // Set the window background color using QPalette::Window
    QPalette palette = rewritePanel->palette();
    palette.setColor(QPalette::Window, QColor(240, 244, 248));  // set winddow background color
    rewritePanel->setAutoFillBackground(true);
    rewritePanel->setPalette(palette);

    // Create a vertical layout for the rewritePanel window
    QVBoxLayout* rewritePanelLayout = new QVBoxLayout(rewritePanel);
    rewritePanelLayout->setContentsMargins(5, 5, 5, 5); // Set the layout's padding to give the button a small space from the rewritePanel edge.
    rewritePanelLayout->setSpacing(2); // Set the spacing between buttons to make them more compact

    QPushButton* rewriteButton=new QPushButton("Rewrite",rewritePanel);
    QPushButton* rewriteButtonWithPrompt=new QPushButton("Rewrite with prompt",rewritePanel);
    rewritePanelLayout->addWidget(rewriteButton);
    rewritePanelLayout->addWidget(rewriteButtonWithPrompt);
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
    // Make the rewritePanel window automatically resize according to the content of its layout
    rewritePanel->adjustSize();
    connect(rewriteButton, &QPushButton::clicked, this, [this](){
        // 统一用 pendingTextForRewrite
        if (pendingTextForRewrite.trimmed().isEmpty()) {
            QMessageBox::warning(this, "Warning", "没有可重写的文本。");
            return;
        }
        setIsRewriteFlowActive(false);
        hideRewriteUI();
        aiClient->sendRewriteRequest(pendingTextForRewrite);
        pendingTextForRewrite.clear();
    });
    connect(rewriteButtonWithPrompt, &QPushButton::clicked, this, &MainWindow::onRewriteWithPromptClicked);
    //跟随定位
    panelAnchorTimer = new QTimer(this);
    panelAnchorTimer->setInterval(200);
    connect(panelAnchorTimer, &QTimer::timeout, this, [this](){
        // if(!rewritePanel->isVisible()){return;}
        // // 1) 找当前焦点窗口
        // GUITHREADINFO gi{sizeof(GUITHREADINFO)};
        // HWND hWndFocus=(GetGUIThreadInfo(NULL,&gi)?gi.hwndFocus:NULL);
        // if(!hWndFocus||!g_pAutomation) return;
        //  // 2) 用 UIA 拿当前焦点输入控件的矩形（不取文本，只取位置，足够轻量）
        // IUIAutomationElement* pElement=NULL;
        // HRESULT hr=g_pAutomation->ElementFromHandle(hWndFocus,&pElement);
        // if(FAILED(hr)||!pElement) return;
        // IUIAutomationElement* controlElement=NULL;

        // controlElement=findChildEditControl(pElement);
        // if(!controlElement) {controlElement=pElement; controlElement->AddRef();}

        // RECT r{};
        // hr=controlElement->get_CurrentBoundingRectangle(&r);

        // if (SUCCEEDED(hr)) {
        //     lastCapturedRect = QRect(r.left, r.top, r.right - r.left, r.bottom - r.top);
        // }

        // // 释放
        // controlElement->Release(); controlElement=nullptr;
        // pElement->Release(); pElement=nullptr;

        //  // 3) 用最新的 lastCapturedRect 计算位置并移动
        // if(!lastCapturedRect.isValid()){return;}
        // QScreen *screen = QApplication::primaryScreen();
        // qreal dpr=screen?screen->devicePixelRatio():1.0;
        // QPoint br=lastCapturedRect.bottomRight()/dpr;
        // rewritePanel->adjustSize();
        // QPoint pos=br-QPoint(rewritePanel->width() + 10, rewritePanel->height() + 10);
        // rewritePanel->move(pos);
        if (!rewritePanel || !rewritePanel->isVisible()) return;
        if (!g_pAutomation) return;

        // 1) 用 UIA 拿真正的焦点元素（跨 Chrome/Edge 更稳定）
        IUIAutomationElement* focused = getFocusedUIAElement();
        if (!focused) return;

        // —— 过滤掉“我们自己的窗口/面板” —— //
        bool isSelfWindow = false;
        // A) 先看进程号（覆盖“无 HWND 的子控件”）
        VARIANT vPid; VariantInit(&vPid);
        if (SUCCEEDED(focused->GetCurrentPropertyValue(UIA_ProcessIdPropertyId, &vPid))) {
            if (vPid.vt == VT_I4 && (DWORD)vPid.lVal == GetCurrentProcessId()) {
                isSelfWindow = true;
            }
            VariantClear(&vPid);
        }

        // B) 再看原来的 HWND 判定（双保险）
        if (!isSelfWindow) {
            VARIANT vHandle; VariantInit(&vHandle);
            if (SUCCEEDED(focused->GetCurrentPropertyValue(UIA_NativeWindowHandlePropertyId, &vHandle))) {
                if (vHandle.vt == VT_I4) {
                    HWND hWnd = (HWND)(LONG_PTR)vHandle.lVal;
                    if (hWnd == (HWND)rewritePanel->winId() || hWnd == (HWND)this->winId()) {
                        isSelfWindow = true;
                    } else {
                        DWORD pid = 0; GetWindowThreadProcessId(hWnd, &pid);
                        if (pid == GetCurrentProcessId()) isSelfWindow = true;
                    }
                }
                VariantClear(&vHandle);
            }
        }
        if (isSelfWindow) { focused->Release(); return; }
        // 2) 取它的边界矩形
        RECT r{};
        HRESULT hr = focused->get_CurrentBoundingRectangle(&r);
        if (SUCCEEDED(hr)) {
            lastCapturedRect = QRect(r.left, r.top, r.right - r.left, r.bottom - r.top);
        }
        focused->Release(); focused = nullptr;

        // 3) 用最新的 lastCapturedRect 重新定位面板
        if (!lastCapturedRect.isValid()) return;
        QScreen* screen = QApplication::primaryScreen();
        qreal dpr = screen ? screen->devicePixelRatio() : 1.0;
        rewritePanel->adjustSize();
        QPoint br = lastCapturedRect.bottomRight() / dpr;
        QPoint pos = br - QPoint(rewritePanel->width() + 10, rewritePanel->height() + 10);
        rewritePanel->move(pos);

    });
    //自动隐藏定时器（复制模式下用）
    panelAutoHideTimer=new QTimer;
    panelAutoHideTimer->setSingleShot(true);
    connect(panelAutoHideTimer,&QTimer::timeout,this,[this](){
        hideRewriteUI();
        pendingTextForRewrite.clear();
    });
}

void MainWindow::showRewriteUI(RewriteUIPlacement placement, const QRect &rect, const QString &text)
{
    ensureRewritePanelInit();
    if(!text.isEmpty()){
        pendingTextForRewrite=text;
    }
    else{
        pendingTextForRewrite=isClipboardEvent?lastClipboardText:lastCapturedText;
    }

    //没有文本就不显示
    if(pendingTextForRewrite.trimmed().isEmpty()) return;
    setIsRewriteFlowActive(true);

    //计算位置
    QScreen* s=QApplication::primaryScreen();
    qreal dpr=s?s->devicePixelRatio():1.0;
    QPoint pos=QCursor::pos();
    const int maxLogical=1000;

    if(placement==RewriteUIPlacement::AnchorToRect){
        QRect r=rect.isNull()?lastCapturedRect:rect;
        if(r.isValid()&&
            r.width()/dpr<maxLogical&&
            r.height()/dpr<maxLogical){
            rewritePanel->adjustSize();
            QPoint br=r.bottomRight()/dpr;
            pos=br-QPoint(rewritePanel->width()+10,rewritePanel->height()+10);
        }
    }
    rewritePanel->move(pos);
    rewritePanel->show();

    // 新增：复制模式（鼠标处）→ 自动隐藏；吸附模式 → 取消隐藏计时
    if (panelAutoHideTimer){
        if (placement == RewriteUIPlacement::AtCursor){
            panelAutoHideTimer->start(2500);
            if (panelAnchorTimer && panelAnchorTimer->isActive()) panelAnchorTimer->stop();
        }else{
            panelAutoHideTimer->stop();
            if (panelAnchorTimer && !panelAnchorTimer->isActive()) panelAnchorTimer->start();

        }
    }
}

void MainWindow::hideRewriteUI()
{
    if (rewritePanel) rewritePanel->hide();
    setIsRewriteFlowActive(false);//复位，让下一次 processCapturedText 能继续走
}

IUIAutomationElement *MainWindow::getFocusedUIAElement()
{
    if(!g_pAutomation) return nullptr;
    IUIAutomationElement* focused=nullptr;
    HRESULT hr=g_pAutomation->GetFocusedElement(&focused);
    if(SUCCEEDED(hr)&&focused!=nullptr){
        return focused;
    }
    return nullptr;
}

void MainWindow::checkClipboard()
{
    if (!isClipboardMonitorEnabled) {
        return;
    }
    QClipboard* clipboard=QApplication::clipboard();
    QString currentText=clipboard->text();

    if(currentText!=previousClipboardText && !currentText.isEmpty()){
        previousClipboardText = currentText;
        lastCapturedText=currentText;
        lastClipboardText=currentText;
        qDebug() << "Clipboard content changed. Starting typing timer for rewrite prompt.";
        showRewriteUI(RewriteUIPlacement::AtCursor, QRect(), lastClipboardText);
    }
}


void MainWindow::onRewriteWithPromptClicked()
{
    bool ok = false;
    QString prompt = QInputDialog::getText(rewritePanel, "Enter Prompt",
                                           "Please enter the prompt:",
                                           QLineEdit::Normal, "", &ok);
    if (!ok || prompt.trimmed().isEmpty()) {
        QMessageBox::warning(this, "Warning", "Prompt cannot be empty!");
        return;
    }
    if (pendingTextForRewrite.trimmed().isEmpty()) {
        QMessageBox::warning(this, "Warning", "没有可重写的文本。");
        return;
    }
    hideRewriteUI();
    setIsRewriteFlowActive(false);
    aiClient->sendRewriteRequest(pendingTextForRewrite, prompt);
    pendingTextForRewrite.clear();
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
    // 允许后续再次触发弹窗
    lastCapturedText.clear();

    if (isClipboardEvent) {
        QClipboard *clipboard = QApplication::clipboard();
        clipboard->setText(rewritedContent);
        QApplication::beep();
    } else {
        // 优先通过 UIA 写回；失败时，内部会走粘贴兜底
        setControlTextUIA(editBlock, rewritedContent);
        if (editBlock) { editBlock->Release(); editBlock = nullptr; }
        else{
            // 没有可用的 UIA 目标，就兜底粘贴
            pasteTextIntoActiveControl(rewritedContent);
        }
    }
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
