#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include "reportgenerator.h"
#include <QFileDialog>
#include <QFile>
#include <QMenu>
#include <QMessageBox>
#include <QDir>
#include <QApplication>



/**
 * @brief Список расширений, как в исходном PowerShell-скрипте.
 */
static QStringList defaultIncludeExt()
{
    return {
        ".ps1",".psm1",".psd1",".bat",".cmd",
        ".txt",".md",".json",".xml",".yaml",".yml",".csv",".ini",".config",
        ".cs",".vb",".fs",".cpp",".hpp",".c",".h",
        ".py",".rb",".go",".ts",".tsx",".js",".jsx",".html",".css"
    };
}

/**
 * @brief Список папок-исключений, как в исходном PowerShell-скрипте.
 */
static QStringList defaultExcludeDirs()
{
    return {
        ".git","node_modules","bin","obj",".vs",".vscode",".idea",".venv","venv",
        "dist","build",".terraform",".cache",".pytest_cache"
    };
}





MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // Подключаем кнопки.
    connect(ui->pbOpen, &QPushButton::clicked, this, &MainWindow::onOpenClicked);
    connect(ui->pbBuild, &QPushButton::clicked, this, &MainWindow::onBuildClicked);
    connect(ui->pbSave, &QPushButton::clicked, this, &MainWindow::onSaveClicked);

    // Контекстное меню для QTextEdit (ПКМ -> Копировать).
    ui->teReprt->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->teReprt, &QTextEdit::customContextMenuRequested,
            this, &MainWindow::onReportContextMenuRequested);

    refreshUiState();
    setStatus(QStringLiteral("Выберите каталог и нажмите «Собрать отчёт»."));
}

MainWindow::~MainWindow()
{
    delete ui;
}



void MainWindow::refreshUiState()
{
    ui->pbBuild->setEnabled(!m_rootDir.isEmpty());
    ui->pbSave->setEnabled(!ui->teReprt->toPlainText().isEmpty());
}

void MainWindow::setStatus(const QString& text)
{
    if (ui->statusbar)
        ui->statusbar->showMessage(text);
}

void MainWindow::onOpenClicked()
{
    const QString startDir = m_rootDir.isEmpty() ? QDir::homePath() : m_rootDir;

    const QString dir = QFileDialog::getExistingDirectory(
        this,
        QStringLiteral("Выберите каталог"),
        startDir,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
        );

    if (dir.isEmpty())
        return;

    m_rootDir = QDir::cleanPath(dir);
    setStatus(QStringLiteral("Каталог выбран: %1").arg(m_rootDir));
    refreshUiState();
}

void MainWindow::onBuildClicked()
{
    if (m_rootDir.isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("Нет каталога"),
                             QStringLiteral("Сначала выберите каталог (кнопка «Открыть»)."));
        return;
    }

    // Параметры генерации. При желании можно вынести в UI.
    ReportGenerator::Options opt;
    opt.rootPath = m_rootDir;
    opt.includeExt = defaultIncludeExt();
    opt.excludeDirNames = defaultExcludeDirs();
    opt.maxBytes = 1024 * 1024;
    opt.useCmdTree = !ui->cbUseCmdTree->isChecked();

    ReportGenerator gen(opt);

    QApplication::setOverrideCursor(Qt::WaitCursor);
    setStatus(QStringLiteral("Генерация отчёта…"));

    QString error;
    const QString report = gen.generate(&error);

    QApplication::restoreOverrideCursor();

    if (report.isEmpty() && !error.isEmpty())
    {
        QMessageBox::critical(this, QStringLiteral("Ошибка"), error);
        setStatus(error);
        return;
    }

    if (!error.isEmpty())
    {
        // Ошибка не критична (например, tree не запустился), отчёт всё равно сгенерирован.
        setStatus(QStringLiteral("Отчёт сгенерирован с предупреждением: %1").arg(error));
    }
    else
    {
        setStatus(QStringLiteral("Отчёт готов."));
    }

    ui->teReprt->setPlainText(report);
    refreshUiState();
}

void MainWindow::onSaveClicked()
{
    const QString text = ui->teReprt->toPlainText();
    if (text.isEmpty())
    {
        QMessageBox::information(this, QStringLiteral("Нечего сохранять"),
                                 QStringLiteral("Сначала сформируйте отчёт."));
        return;
    }

    QString suggested = m_lastSavePath;
    if (suggested.isEmpty())
    {
        if (!m_rootDir.isEmpty())
            suggested = QDir(m_rootDir).filePath("report.md");
        else
            suggested = QDir::home().filePath("report.md");
    }

    QString fileName = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("Сохранить отчёт"),
        suggested,
        QStringLiteral("Markdown (*.md);;Text (*.txt);;All Files (*.*)")
        );

    if (fileName.isEmpty())
        return;

    // Если пользователь не указал расширение — по умолчанию .md
    if (QFileInfo(fileName).suffix().isEmpty())
        fileName += ".md";

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        QMessageBox::critical(this, QStringLiteral("Ошибка сохранения"),
                              QStringLiteral("Не удалось открыть файл для записи:\n%1").arg(file.errorString()));
        return;
    }

    // Пишем UTF-8 с BOM (чаще удобнее для Windows/Notepad).
    QByteArray out;
    out.append(char(0xEF));
    out.append(char(0xBB));
    out.append(char(0xBF));
    out.append(text.toUtf8());

    file.write(out);
    file.close();

    m_lastSavePath = fileName;
    setStatus(QStringLiteral("Сохранено: %1").arg(fileName));
}

void MainWindow::onReportContextMenuRequested(const QPoint& pos)
{
    QMenu menu(this);

    QAction* copyAction = menu.addAction(QStringLiteral("Копировать"));
    copyAction->setEnabled(ui->teReprt->textCursor().hasSelection());
    connect(copyAction, &QAction::triggered, ui->teReprt, &QTextEdit::copy);

    // (Опционально) можно добавить "Выделить всё" — обычно удобно.
    QAction* selectAllAction = menu.addAction(QStringLiteral("Выделить всё"));
    connect(selectAllAction, &QAction::triggered, ui->teReprt, &QTextEdit::selectAll);

    menu.exec(ui->teReprt->mapToGlobal(pos));
}
