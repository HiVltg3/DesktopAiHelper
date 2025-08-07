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
    QWidget* popup=nullptr;//popup rewrite widget

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
    void showRewritePrompt(const QString& copiedText);

    //install event filter for userinput
    void setupEventFilter();
    void updateScrollArea();// 强制更新布局

    virtual bool eventFilter(QObject *watched, QEvent *event) override;
public:
    //rewrite function related
    void startClipboardMonitoring();
    void stopClipboardMonitoring();

    //windows api
    // =============================================//

    //UIA
    void initializeUIA();
    void uninitializeUIA();
    IUIAutomationElement* findChildEditControl(IUIAutomationElement* parentElement);
    //hook
    bool installGlobalKeyboardHook();
    void uninstallGlobalKeyboardHook();
    QString getControlTextUIAInternal(HWND hwndFocus);

public slots:
    // 用于接收从钩子线程传递过来的文本
    // 必须是 public slot，并且参数类型要匹配 Q_ARG
    void processCapturedText(const QString &text);
    /*最理想的方法是：

    首先，获取 Chrome_WidgetWin_1 窗口对应的 IUIAutomationElement。

    然后，从这个父元素开始，遍历它的子元素树，寻找一个类型为“编辑框”（ControlType 为 UIA_EditControlTypeId）的元素。

    找到这个编辑框元素后，再从它身上获取 ValuePattern 或 TextPattern。

    创建一个新的辅助函数，例如 MainWindow::findChildEditControl(IUIAutomationElement* parentElement)。这个函数将递归地遍历 parentElement 的子元素。

    在 getControlTextUIAInternal() 中，首先获取 Chrome_WidgetWin_1 对应的 pElement。

    然后，调用 findChildEditControl(pElement)，让它返回真正的输入框 IUIAutomationElement*。

    如果找到了，再从这个子元素上获取 ValuePattern 或 TextPattern。*/
    // =============================================//

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

    //rewrite function related
    void checkClipboard();
    void rewriteText();
    void onRewriteWithPromptClicked();
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
