#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include "reportgenerator.h"
#include <QFileDialog>
#include <QFile>
#include <QMenu>
#include <QMessageBox>
#include <QDir>
#include <QApplication>
#include <QRegularExpression>
#include <QSet>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <limits>
#include <QClipboard>
#include <QGuiApplication>





/**
 * @brief Парсит строку размера (например: "1MB", "512KB", "2.5MiB") в байты.
 * @details
 *  Используются бинарные множители как в PowerShell (1MB = 1024*1024).
 *  Поддерживаемые суффиксы (без учёта регистра):
 *   - B (или без суффикса) -> байты
 *   - K, KB, KiB -> 1024
 *   - M, MB, MiB -> 1024^2
 *   - G, GB, GiB -> 1024^3
 *   - T, TB, TiB -> 1024^4
 *
 * @param text Входной текст.
 * @param bytesOut Результат в байтах.
 * @param errorOut (опционально) текст ошибки.
 * @return true если распознано.
 */
static bool parseHumanSizeToBytes(QString text, qint64* bytesOut, QString* errorOut = nullptr)
{
    if (bytesOut) *bytesOut = 0;

    text = text.trimmed();
    if (text.isEmpty())
    {
        if (errorOut) *errorOut = QStringLiteral("пустая строка");
        return false;
    }

    // Для русской раскладки: "1,5MB" -> "1.5MB"
    text.replace(',', '.');

    const QRegularExpression re(QStringLiteral(R"(^\s*([0-9]+(?:\.[0-9]+)?)\s*([A-Za-z]*)\s*$)"));
    const auto m = re.match(text);
    if (!m.hasMatch())
    {
        if (errorOut) *errorOut = QStringLiteral("неверный формат. Пример: 1MB, 512KB, 2.5MiB");
        return false;
    }

    bool ok = false;
    const double value = m.captured(1).toDouble(&ok);
    if (!ok || value <= 0.0)
    {
        if (errorOut) *errorOut = QStringLiteral("число должно быть > 0");
        return false;
    }

    QString suf = m.captured(2).trimmed().toLower();

    long double mul = 1.0L;
    if (suf.isEmpty() || suf == "b" || suf == "byte" || suf == "bytes")
        mul = 1.0L;
    else if (suf == "k" || suf == "kb" || suf == "kib")
        mul = 1024.0L;
    else if (suf == "m" || suf == "mb" || suf == "mib")
        mul = 1024.0L * 1024.0L;
    else if (suf == "g" || suf == "gb" || suf == "gib")
        mul = 1024.0L * 1024.0L * 1024.0L;
    else if (suf == "t" || suf == "tb" || suf == "tib")
        mul = 1024.0L * 1024.0L * 1024.0L * 1024.0L;
    else
    {
        if (errorOut) *errorOut = QStringLiteral("неизвестный суффикс: %1").arg(suf);
        return false;
    }

    const long double bytesLd = (long double)value * mul;

    if (bytesLd > (long double)std::numeric_limits<qint64>::max())
    {
        if (errorOut) *errorOut = QStringLiteral("слишком большое значение");
        return false;
    }

    // Округляем до ближайшего целого байта.
    const qint64 bytes = (qint64)(bytesLd + 0.5L);
    if (bytes <= 0)
    {
        if (errorOut) *errorOut = QStringLiteral("получилось <= 0 байт");
        return false;
    }

    if (bytesOut) *bytesOut = bytes;
    return true;
}


/**
 * @brief Разбирает пользовательский список из текстового поля.
 * @details
 *  Разделители: перевод строки, пробелы/табы, запятая, точка с запятой.
 *  Пустые элементы игнорируются, дубликаты убираются (с сохранением порядка).
 *
 * @param text Исходный текст из QPlainTextEdit/QTextEdit.
 * @param forceDotPrefix Если true — для элементов будет гарантирована точка в начале (для расширений).
 * @param toLower Если true — приводим к нижнему регистру (удобно для сравнения как в PowerShell).
 */
static QStringList parseUserList(const QString& text, bool forceDotPrefix, bool toLower)
{
    const QStringList tokens = text.split(QRegularExpression(QStringLiteral(R"([\s,;]+)")),
                                          Qt::SkipEmptyParts);

    QStringList out;
    QSet<QString> seen;

    for (QString t : tokens)
    {
        t = t.trimmed();
        if (t.isEmpty())
            continue;

        if (forceDotPrefix && !t.startsWith('.'))
            t.prepend('.');

        if (toLower)
            t = t.toLower();

        if (!seen.contains(t))
        {
            seen.insert(t);
            out.push_back(t);
        }
    }
    return out;
}


/**
 * @brief Список расширений, как в исходном PowerShell-скрипте.
 */
static QStringList defaultIncludeExt()
{
    return {
        // Документы
        ".doc",".docx",".pdf",
        // Excel
        ".xls",".xlsx",".xlsm",
        // Qt Designer
        ".ui", ".qrc", ".ts", ".qss", ".pri", ".pro",
        // Скрипты/текст
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
    setWindowTitle(tr("ContextMaker"));


    ui->teReprt->setReadOnly(true);

    // Небольшой стиль для ``` блоков (работает при setMarkdown)
    ui->teReprt->document()->setDefaultStyleSheet(
        "code, pre { font-family: Consolas, 'Courier New', monospace; }"
        "pre { background-color: rgba(127,127,127,0.15); padding: 6px; }"
        );



    ///@{
    ui->leMaxOutChars->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral(R"(^\s*\d+([.,]\d+)?\s*(B|KB|MB|GB|TB|K|M|G|T|KiB|MiB|GiB|TiB)?\s*$)"),
                           QRegularExpression::CaseInsensitiveOption),
        ui->leMaxOutChars));

    ui->leMaxOutChars->setText(QStringLiteral("1MB"));
    ///@}



    // --- Дефолтные значения настроек (как в PowerShell-скрипте) ---
    // Разрешаем ввод вида: 1MB, 512KB, 2.5MiB и т.п.
    auto* v = new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral(R"(^\s*\d+([.,]\d+)?\s*(B|KB|MB|GB|TB|K|M|G|T|KiB|MiB|GiB|TiB)?\s*$)"),
                           QRegularExpression::CaseInsensitiveOption),
        ui->leMaxBytes
        );
    ui->leMaxBytes->setValidator(v);
    ui->leMaxBytes->setText(QStringLiteral("1MB"));

    ui->pteIncludeExt->setPlainText(defaultIncludeExt().join('\n'));
    ui->pteExcludeDir->setPlainText(defaultExcludeDirs().join('\n'));


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
    ui->pbSave->setEnabled(!m_reportMarkdown.isEmpty());
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


static bool parseHumanSizeToBytesAllowZero(QString text, qint64* out, QString* err=nullptr)
{
    const QString t = text.trimmed();
    if (t == "0" || t.compare("0B", Qt::CaseInsensitive) == 0)
    {
        if (out) *out = 0;
        return true;
    }
    return parseHumanSizeToBytes(text, out, err);
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
    // ReportGenerator::Options opt;
    // opt.rootPath = m_rootDir;
    // opt.includeExt = defaultIncludeExt();
    // opt.excludeDirNames = defaultExcludeDirs();
    // opt.maxBytes = 1024 * 1024;
    // Параметры генерации из UI
    ReportGenerator::Options opt;

    qint64 maxOutChars = 0;
    QString outErr;
    if (!parseHumanSizeToBytesAllowZero(ui->leMaxOutChars->text(), &maxOutChars, &outErr))
    {
        QMessageBox::warning(this,
                             QStringLiteral("Неверный лимит вывода"),
                             QStringLiteral("Не удалось разобрать лимит вывода: %1\nПример: 200KB, 1MB, 5MB")
                                 .arg(outErr));
        return;
    }
    opt.maxOutChars = maxOutChars;



    opt.rootPath = m_rootDir;
    opt.treeOnly = ui->cbTreeOnly->isChecked();


    opt.noBomEncodingMode =
        (ui->cbEncodingMode->currentIndex() == 1)
            ? ReportGenerator::Options::NoBomEncodingMode::ForceAnsi
            : ReportGenerator::Options::NoBomEncodingMode::AutoUtf8ThenAnsi;


    qint64 maxBytes = 0;
    QString sizeErr;
    if (!parseHumanSizeToBytes(ui->leMaxBytes->text(), &maxBytes, &sizeErr))
    {
        QMessageBox::warning(this,
                             QStringLiteral("Неверный MaxBytes"),
                             QStringLiteral("Не удалось разобрать MaxBytes: %1\nПример: 1MB, 512KB, 2.5MiB")
                                 .arg(sizeErr));
        return;
    }
    opt.maxBytes = maxBytes;


    opt.includeExt = parseUserList(ui->pteIncludeExt->toPlainText(), /*forceDotPrefix*/true,  /*toLower*/true);
    if (opt.includeExt.isEmpty())
        opt.includeExt = defaultIncludeExt();

    opt.excludeDirNames = parseUserList(ui->pteExcludeDir->toPlainText(), /*forceDotPrefix*/false, /*toLower*/true);
    if (opt.excludeDirNames.isEmpty())
        opt.excludeDirNames = defaultExcludeDirs();

    // ВАЖНО:
    // Если ты сознательно сделал инверсию — оставь как у тебя.
    // Если нет — должно быть: opt.useCmdTree = ui->cbUseCmdTree->isChecked();
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

 //   ui->teReprt->setPlainText(report);

    m_reportMarkdown = report;

#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    ui->teReprt->setMarkdown(m_reportMarkdown);
#else
    ui->teReprt->setPlainText(m_reportMarkdown); // fallback если Qt старый
#endif


    refreshUiState();
}

void MainWindow::onSaveClicked()
{
    //const QString text = ui->teReprt->toPlainText();
    const QString text = m_reportMarkdown.isEmpty()
                             ? ui->teReprt->toPlainText()
                             : m_reportMarkdown;

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

    QAction* copyMdAction = menu.addAction(QStringLiteral("Копировать (Markdown)"));
    copyMdAction->setEnabled(!m_reportMarkdown.isEmpty());
    connect(copyMdAction, &QAction::triggered, this, [this]() {
        QGuiApplication::clipboard()->setText(m_reportMarkdown);
    });
    menu.addSeparator();

    menu.exec(ui->teReprt->mapToGlobal(pos));
}

