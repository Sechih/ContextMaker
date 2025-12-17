#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QFutureWatcher>
#include <QPair>
#include <QPointer>
#include <atomic>

class QProgressDialog;


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
    void onBuildFinished();


private:
    Ui::MainWindow *ui;

    QString m_rootDir;          ///< Выбранный каталог для обхода.
    QString m_lastSavePath;     ///< Последний путь сохранения (для удобства).
    QString m_reportMarkdown; ///< Исходный markdown отчёта (для сохранения в файл).
    bool m_buildInProgress = false; ///< Идёт ли сейчас генерация отчёта.

    std::atomic_bool m_cancelRequested { false }; ///< Флаг отмены для генератора.

    /** \brief Результат фоновой генерации: (report, error). */
    QFutureWatcher<QPair<QString, QString>> m_buildWatcher;

    /** \brief Диалог прогресса (спиннер) на время генерации. */
    QPointer<QProgressDialog> m_progress;

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
