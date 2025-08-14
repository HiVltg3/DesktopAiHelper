#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QJsonArray>
//windows api
#include <Windows.h>
#include <UIAutomation.h>

// 全局钩子句柄 (必须是全局的，因为回调函数不在类中)
// HHOOK 是 Windows API 中钩子的句柄类型
extern HHOOK g_hKeyboardHook;
// 全局的 IUIAutomation 对象指针 (也建议是全局的或静态的，只创建一次)
// 这样做是为了在钩子回调函数中也能访问到 UIA 对象
extern IUIAutomation* g_pAutomation;
// 全局的 MainWindow 实例指针 (用于跨线程通信到主UI线程)
// 钩子回调函数中需要它来 QMetaObject::invokeMethod
extern class MainWindow* g_pMainWindow;

class QItemSelectionModel;
class QStandardItemModel;
class QListView;
class GeminiClient;
class AIClient;
class QLabel;
class QScrollArea;
class QVBoxLayout;
class QPushButton;
QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private:
    Ui::MainWindow *ui;
    //STL for storing chat
    QVector<QVector<QPair<QString, QString>>> conversationHistoriesVector;
    QVBoxLayout* vBoxLayout_ChatHistory;//where displays chat history
    QScrollArea* chatScrollArea;
    QPushButton* closeButton;
    QPushButton* hideButton;

    //setup ui
    QListView* chatHistoryList; //chat history shows on listview
    QItemSelectionModel* chatHistotySelectionModel;
    QStandardItemModel* chatHistoryModel;


    //monitor clipboard timer
    QTimer* clipboardMonitorTimer;
    //save the latest user input
    QString userInput;

    //AI client, we can change to other AIs
    AIClient* aiClient=nullptr;

    //stores all chat data
    QJsonArray allConversations;

    // track the index of current conversation
    int currentConversationIndex = -1;

    //saves the previous ClipboardText
    QString previousClipboardText;

    //Models
    enum class AI_Models{
        ChatGPT,
        Gemini
        //add ai models here
    };
//-------------------------------rewrite related-------------------------------//
private:
    QWidget* rewritePanel = nullptr;
    QTimer*  panelAnchorTimer = nullptr;   // 跟随定位，200ms 刷一次
    QString  pendingTextForRewrite;        // 当前要重写的文本
    //windows api(rewrite related)
    QTimer* typingTimer;// 用于检测输入停顿的计时器
    QString lastCapturedText;// 存储上次捕获的文本
    QString lastClipboardText;//存储上次剪贴板的文本
    QPoint lastMousePosition; // 存储上次鼠标位置
    QRect lastCapturedRect;// 用于存储上次捕获的输入框坐标
    IUIAutomationElement* editBlock=nullptr;
    bool isClipboardEvent = false; // 一个标志位，用于区分事件来源
    bool isClipboardMonitorEnabled = false;//一个开关，用于控制是否启用复制检测功能
    bool isRewriteFlowActive = false; // 新增一个标志位
    QTimer* panelAutoHideTimer = nullptr;//自动隐藏计时器
    enum class RewriteUIPlacement { AnchorToRect, AtCursor };
    void showRewritePrompt(const QString copiedText);
    void ensureRewritePanelInit();
    void showRewriteUI(RewriteUIPlacement placement,const QRect& rect=QRect(),const QString& text=QString());
    void hideRewriteUI();
    IUIAutomationElement* getFocusedUIAElement();
public:
    void startClipboardMonitoring();
    void stopClipboardMonitoring();
    bool getIsRewriteFlowActive() const{return isRewriteFlowActive;}
    void setIsRewriteFlowActive(bool active) {isRewriteFlowActive=active;}

    //windows api
    //UIA
    void initializeUIA();
    void uninitializeUIA();
    IUIAutomationElement* getEditBlock() { return editBlock; }
    //hook
    bool installGlobalKeyboardHook();
    void uninstallGlobalKeyboardHook();
    void getControlTextUIAInternal(HWND hwndFocus);//文本和坐标的传递逻辑将由它来封装，它不再需要返回任何东西。
    IUIAutomationElement* findChildEditControl(IUIAutomationElement* parentElement);
    void setControlTextUIA(IUIAutomationElement* element, const QString& text);
    void pasteTextIntoActiveControl(const QString& text);
public slots:
    // 用于接收从钩子线程传递过来的文本
    // 必须是 public slot，并且参数类型要匹配 Q_ARG
    void processCapturedText(const QString &text,const QRect& rect);
    void onTypingPause();// 在输入停顿时执行的槽函数
private slots:
    //rewrite function related
    void checkClipboard();
    void onRewriteWithPromptClicked();
    void onTranslateButtonClicked();
//-------------------------------rewrite related-------------------------------//

private: //functions
    void loadChatHistory(QStandardItemModel *model); //load chat history
    QString getTextColorBasedOnTheme();
    void loadQSS();
    void UISetup();
    void GeminiSetup();
    void ChatGPTSetup();
    void saveChatHistory(); // save chat history to local file
    QWidget* createMessageBubble(const QString& htmlMessage,bool isUser);
    QString plainTextToHtml(const QString& message);
    void displayUserMessage(const QString &message); // displays users new msg
    void displayAIMessage(const QString &message); // displays Ai's new msg
    void displayAIMessage(const QPixmap& image);// displays Ai's pic

    void deleteVBoxChildren();
    //rewrite function related

    //install event filter for userinput
    void setupEventFilter();
    void updateScrollArea();// 强制更新布局

    virtual bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void do_showChatHistory(const QModelIndex &index);

    // AIClient slots
    void handleAiResponse(const QString &response);
    void handleAiError(const QString &errorMsg);
    void handleTitleGenerated(const QString &title);
    void handleRewritedContent(const QString& rewritedContent);
    void on_button_sendText_clicked();
    void on_button_newChat_clicked();
    void handlePicContent(const QPixmap& image);

    // QWidget interface
    void on_comboBox_currentIndexChanged(int index);

protected:
    virtual void closeEvent(QCloseEvent *event) override;

protected:
    //drag widget
    virtual void mousePressEvent(QMouseEvent *event) override;
    virtual void mouseReleaseEvent(QMouseEvent *event) override;
    virtual void mouseMoveEvent(QMouseEvent *event) override;
    //displays close button
    virtual void resizeEvent(QResizeEvent *event) override;
private:
    QPoint mousePoint;
    bool mouse_press;

    // QWidget interface

};







#endif // MAINWINDOW_H
