#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

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

private slots:
    void onOpenClicked();
    void onBuildClicked();
    void onSaveClicked();
    void onReportContextMenuRequested(const QPoint& pos);


private:
    Ui::MainWindow *ui;

    QString m_rootDir;          ///< Выбранный каталог для обхода.
    QString m_lastSavePath;     ///< Последний путь сохранения (для удобства).

    /**
     * @brief Включить/выключить кнопки в зависимости от состояния.
     */
    void refreshUiState();

    /**
     * @brief Показать сообщение в статус-баре.
     */
    void setStatus(const QString& text);
};
#endif // MAINWINDOW_H
