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

    //save the latest user input
    QString userInput;

    //AI client, we can change to other AIs
    AIClient* aiClient=nullptr;

    //stores all chat data
    QJsonArray allConversations;

    // track the index of current conversation
    int currentConversationIndex = -1;

private: //functions
    void loadChatHistory(QStandardItemModel *model); //load chat history
    QString getTextColorBasedOnTheme();
    void UISetup();
    void GeminiSetup();
    void saveChatHistory(); // 新增：保存聊天历史到文件
    void displayUserMessage(const QString &message); // displays users new msg
    void displayAIMessage(const QString &message); // displays Ai's new msg

private slots:
    void do_showChatHistory(const QModelIndex &index);

    // AIClient slots
    void handleAiResponse(const QString &response);
    void handleAiError(const QString &errorMsg);
    void handleTitleGenerated(const QString &title);
    void handleRewritedContent(const QString& rewritedContent);
    void on_button_sendText_clicked();
    void on_button_newChat_clicked();

    // QWidget interface
protected:
    virtual void closeEvent(QCloseEvent *event) override;
};


#endif // MAINWINDOW_H
