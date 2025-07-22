#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QJsonArray>
class QItemSelectionModel;
class QStandardItemModel;
class QListView;
class GeminiClient;
class AIClient;
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

    //setup ui
    QListView* chatHistoryList; //chat history shows on listview
    QItemSelectionModel* chatHistotySelectionModel;
    QStandardItemModel* chatHistoryModel;
    QWidget* popup=nullptr;//popup rewrite widget

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
    void UISetup();
    void GeminiSetup();
    void ChatGPTSetup();
    void saveChatHistory(); // save chat history to local file
    void displayUserMessage(const QString &message); // displays users new msg
    void displayAIMessage(const QString &message); // displays Ai's new msg

    //rewrite function related
    void startClipboardMonitoring();
    void showRewritePrompt(const QString& copiedText);

    //install event filter for userinput
    void setupEventFilter();
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

    //rewrite function related
    void checkClipboard();
    void rewriteText();
    void onRewriteWithPromptClicked();
    // QWidget interface
    void on_comboBox_currentIndexChanged(int index);

protected:
    virtual void closeEvent(QCloseEvent *event) override;

    // QWidget interface
protected:

    // QObject interface

};





#endif // MAINWINDOW_H
