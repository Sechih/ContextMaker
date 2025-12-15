/**
 * @file reportgenerator.cpp
 * @brief Реализация генератора отчёта.
 */

#include "reportgenerator.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QtGlobal>
#include <algorithm>
#include <utility>
#include <QTemporaryDir>
#include <QXmlStreamReader>
#include <QCoreApplication>
#include <QHash>
#include <QMap>
#include <QStandardPaths>
#include <windows.h>
#include <limits>

static void truncateWithNote(QString& s, qint64 maxChars, const QString& note)
{
    if (maxChars <= 0)
        return;

    const int lim = (maxChars > (qint64)std::numeric_limits<int>::max())
                        ? std::numeric_limits<int>::max()
                        : (int)maxChars;

    if (s.size() <= lim)
        return;

    QString suffix = QStringLiteral("\n") + note + QStringLiteral("\n");

    // Если даже suffix больше лимита — оставляем только кусок suffix
    if (suffix.size() >= lim)
    {
        suffix.truncate(lim);
        s = suffix;
        return;
    }

    const int keep = lim - suffix.size();
    s.truncate(keep);
    s += suffix; // итог ровно <= lim
}

#ifdef Q_OS_WIN

static QString decodeOem(const QByteArray& bytes)
{
    if (bytes.isEmpty())
        return {};

    const UINT cp = GetOEMCP(); // OEM codepage (для RU чаще 866)

    const int wlen = MultiByteToWideChar(cp, 0,
                                         bytes.constData(), bytes.size(),
                                         nullptr, 0);
    if (wlen <= 0)
        return QString();

    QString out(wlen, Qt::Uninitialized);

    MultiByteToWideChar(cp, 0,
                        bytes.constData(), bytes.size(),
                        reinterpret_cast<wchar_t*>(out.data()), wlen);

    return out;
}
#endif


static QString makeMarkdownFence(const QString& text)
{
    // Найдём максимальную длину подряд идущих ` в тексте
    int maxRun = 0;
    int run = 0;

    for (QChar ch : text)
    {
        if (ch == QLatin1Char('`'))
        {
            ++run;
            if (run > maxRun) maxRun = run;
        }
        else
        {
            run = 0;
        }
    }

    // Делаем fence строго длиннее любого run в контенте (минимум 3)
    const int fenceLen = std::max(3, maxRun + 1);
    return QString(fenceLen, QLatin1Char('`'));
}

/**
 * @brief Утилита: безопасно получить имя директории по абсолютному пути.
 */
static QString dirNameFromPath(const QString& absPath)
{
    return QFileInfo(absPath).fileName();
}

ReportGenerator::ReportGenerator(const Options& opt)
    : m_opt(opt)
{
    // Нормализуем набор расширений.
    for (const QString& ext : std::as_const(m_opt.includeExt))
    {
        QString e = ext.trimmed();
        if (!e.isEmpty() && !e.startsWith('.'))
            e.prepend('.');
        m_includeSet.insert(e.toLower());
    }

    for (const QString& name : std::as_const(m_opt.excludeDirNames))
    {
        const QString n = name.trimmed();
        if (!n.isEmpty())
            m_excludeSet.insert(n.toLower());  // <-- лучше сразу lower, как PowerShell (case-insensitive)
    }

}


#ifdef Q_OS_WIN
const bool onWindows = true;
#else
const bool onWindows = false;
#endif


QString ReportGenerator::generate(QString* errorOut) const
{
    if (m_opt.rootPath.trimmed().isEmpty())
    {
        if (errorOut) *errorOut = QStringLiteral("Не задан корневой каталог.");
        return {};
    }

    const QString root = QDir::cleanPath(m_opt.rootPath);

    if (!QFileInfo::exists(root) || !QFileInfo(root).isDir())
    {
        if (errorOut) *errorOut = QStringLiteral("Каталог не найден: %1").arg(root);
        return {};
    }

    QStringList lines;
    lines << QStringLiteral("# Отчёт по каталогу: %1").arg(root);
    lines << QStringLiteral("## 1. Дерево каталогов и файлов");
    lines << QStringLiteral("```text");

    // Если корневая папка сама в списке исключений — отчёт будет пустым (как в PS-скрипте).
    const QString rootName = QFileInfo(root).fileName();
    const bool rootExcluded = m_excludeSet.contains(rootName.toLower());


    if (!rootExcluded)
    {
        if (m_opt.useCmdTree && onWindows)
        {
            QString treeErr;
            const QString treeOut = runCmdTree(&treeErr);
            const QString treeOutTrim = treeOut.trimmed();

            if (!treeOutTrim.isEmpty())
            {
                lines << treeOutTrim;
            }
            else
            {
                // Фолбэк на внутренний генератор дерева.
                QStringList treeLines;
                showTreeRec(root, QString(), treeLines);
                lines << treeLines;

                if (errorOut && !treeErr.isEmpty())
                    *errorOut = treeErr;
            }
        }
        else
        {
            // Встроенное дерево (без внешних утилит)
            QStringList treeLines;
            showTreeRec(root, QString(), treeLines);
            lines << treeLines;
        }
    }

    lines << QStringLiteral("```");

    // Если включён режим "только дерево" — возвращаем отчёт прямо тут
    if (m_opt.treeOnly)
        return lines.join('\n');


    lines << QString(); // пустая строка

    lines << QStringLiteral("## 2. Содержимое файлов (отфильтровано)");
    lines << QStringLiteral("*(выводятся только текстовые файлы из IncludeExt и не больше %1 байт)*").arg(m_opt.maxBytes);
    lines << QString();

    if (!rootExcluded)
    {
        QVector<QFileInfo> files;
        collectFilesRec(root, files);

        // Сортировка по полному пути.
        std::sort(files.begin(), files.end(), [](const QFileInfo& a, const QFileInfo& b){
            return a.absoluteFilePath().compare(b.absoluteFilePath(), Qt::CaseInsensitive) < 0;
        });

        const QDir rootDir(root);

        for (const QFileInfo& f : std::as_const(files))
        {
            const QString abs = f.absoluteFilePath();
            QString rel = rootDir.relativeFilePath(abs);
            rel = QDir::toNativeSeparators(rel);

            QString readErr;
            QString content = readFileForReport(f, &readErr);

            QString payload;
            payload.reserve(256 + content.size());
            payload += QStringLiteral("----- BEGIN FILE: %1 [%2 bytes] ----\n").arg(rel).arg(f.size());

            if (!readErr.isEmpty())
                payload += QStringLiteral("[ОШИБКА ЧТЕНИЯ: %1]\n").arg(readErr);
            else
                payload += content + QLatin1Char('\n');

            payload += QStringLiteral("----- END FILE:   %1 ----\n").arg(rel);

            // Выбираем безопасный fence под конкретный payload
            const QString fence = makeMarkdownFence(payload);

            lines << (fence + QStringLiteral("text"));
            lines << payload.trimmed();
            lines << fence;
            lines << QString();

        }
    }

    return lines.join('\n');
}

bool ReportGenerator::isUnderExcluded(const QFileInfo& item) const
{
    // Берём директорию: для файла — его папка, для папки — она сама.
    QDir dir = item.isDir() ? QDir(item.absoluteFilePath()) : item.dir();

    while (true)
    {
        const QString name = dirNameFromPath(dir.absolutePath()).toLower();
        if (!name.isEmpty() && m_excludeSet.contains(name))
            return true;

        const QString before = dir.absolutePath();
        if (!dir.cdUp())
            break;

        if (dir.absolutePath() == before)
            break;
    }
    return false;
}

bool ReportGenerator::shouldIncludeFile(const QFileInfo& file) const
{
    if (!file.isFile())
        return false;

    // Максимальный размер.
    if (file.size() > m_opt.maxBytes)
        return false;

    // Расширение (как в PowerShell FileInfo.Extension: только последняя часть).
    const QString suffix = file.suffix();
    const QString ext = suffix.isEmpty() ? QString() : QStringLiteral(".%1").arg(suffix);
    return m_includeSet.contains(ext.toLower());
}

QString ReportGenerator::readTextSmart(const QString& path, QString* errorOut) const
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
    {
        if (errorOut) *errorOut = f.errorString();
        return {};
    }

    const QByteArray bytes = f.readAll();
    if (bytes.isEmpty())
        return QString();

    // --- BOM detection ---
    auto startsWith = [&](std::initializer_list<unsigned char> sig) -> bool {
        if (bytes.size() < (int)sig.size()) return false;
        int i = 0;
        for (unsigned char b : sig)
        {
            if ((unsigned char)bytes.at(i) != b) return false;
            ++i;
        }
        return true;
    };

    // UTF-8 BOM
    if (startsWith({0xEF, 0xBB, 0xBF}))
        return QString::fromUtf8(bytes.constData() + 3, bytes.size() - 3);

    // UTF-32 LE BOM: FF FE 00 00
    if (startsWith({0xFF, 0xFE, 0x00, 0x00}))
    {
        const QByteArray payload = bytes.mid(4);

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        QVector<char32_t> cps;
        cps.reserve(payload.size() / 4);

        for (int i = 0; i + 3 < payload.size(); i += 4)
        {
            const uint cp =
                (uint)(unsigned char)payload[i] |
                ((uint)(unsigned char)payload[i + 1] << 8) |
                ((uint)(unsigned char)payload[i + 2] << 16) |
                ((uint)(unsigned char)payload[i + 3] << 24);

            cps.push_back(static_cast<char32_t>(cp));
        }

        return QString::fromUcs4(cps.constData(), cps.size());
#else
        QVector<uint> cps;
        cps.reserve(payload.size() / 4);

        for (int i = 0; i + 3 < payload.size(); i += 4)
        {
            const uint cp =
                (uint)(unsigned char)payload[i] |
                ((uint)(unsigned char)payload[i + 1] << 8) |
                ((uint)(unsigned char)payload[i + 2] << 16) |
                ((uint)(unsigned char)payload[i + 3] << 24);

            cps.push_back(cp);
        }

        return QString::fromUcs4(cps.constData(), cps.size());
#endif
    }

    // UTF-32 BE BOM: 00 00 FE FF
    if (startsWith({0x00, 0x00, 0xFE, 0xFF}))
    {
        const QByteArray payload = bytes.mid(4);

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        QVector<char32_t> cps;
        cps.reserve(payload.size() / 4);

        for (int i = 0; i + 3 < payload.size(); i += 4)
        {
            const uint cp =
                ((uint)(unsigned char)payload[i] << 24) |
                ((uint)(unsigned char)payload[i + 1] << 16) |
                ((uint)(unsigned char)payload[i + 2] << 8) |
                (uint)(unsigned char)payload[i + 3];

            cps.push_back(static_cast<char32_t>(cp));
        }

        return QString::fromUcs4(cps.constData(), cps.size());
#else
        QVector<uint> cps;
        cps.reserve(payload.size() / 4);

        for (int i = 0; i + 3 < payload.size(); i += 4)
        {
            const uint cp =
                ((uint)(unsigned char)payload[i] << 24) |
                ((uint)(unsigned char)payload[i + 1] << 16) |
                ((uint)(unsigned char)payload[i + 2] << 8) |
                (uint)(unsigned char)payload[i + 3];

            cps.push_back(cp);
        }

        return QString::fromUcs4(cps.constData(), cps.size());
#endif
    }


    // UTF-16 LE BOM: FF FE
    if (startsWith({0xFF, 0xFE}))
    {
        const QByteArray payload = bytes.mid(2);

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        QVector<char16_t> u16;
        u16.reserve(payload.size() / 2);

        for (int i = 0; i + 1 < payload.size(); i += 2)
        {
            const char16_t cu = static_cast<char16_t>(
                (unsigned char)payload[i] |
                ((unsigned char)payload[i + 1] << 8)
                );
            u16.push_back(cu);
        }

        return QString::fromUtf16(u16.constData(), u16.size());
#else
        QVector<ushort> u16;
        u16.reserve(payload.size() / 2);

        for (int i = 0; i + 1 < payload.size(); i += 2)
        {
            const ushort cu = (ushort)(
                (unsigned char)payload[i] |
                ((unsigned char)payload[i + 1] << 8)
                );
            u16.push_back(cu);
        }

        return QString::fromUtf16(u16.constData(), u16.size());
#endif
    }


    // UTF-16 BE BOM: FE FF
    if (startsWith({0xFE, 0xFF}))
    {
        const QByteArray payload = bytes.mid(2);

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        QVector<char16_t> u16;
        u16.reserve(payload.size() / 2);

        for (int i = 0; i + 1 < payload.size(); i += 2)
        {
            const char16_t cu = static_cast<char16_t>(
                ((unsigned char)payload[i] << 8) |
                (unsigned char)payload[i + 1]
                );
            u16.push_back(cu);
        }

        return QString::fromUtf16(u16.constData(), u16.size());
#else
        QVector<ushort> u16;
        u16.reserve(payload.size() / 2);

        for (int i = 0; i + 1 < payload.size(); i += 2)
        {
            const ushort cu = (ushort)(
                ((unsigned char)payload[i] << 8) |
                (unsigned char)payload[i + 1]
                );
            u16.push_back(cu);
        }

        return QString::fromUtf16(u16.constData(), u16.size());
#endif
    }

    // --- Режим "принудительно ANSI" применяется только если BOM не найден ---
    if (m_opt.noBomEncodingMode == Options::NoBomEncodingMode::ForceAnsi)
        return QString::fromLocal8Bit(bytes);

    // --- UTF-8 без BOM (строго) ---
    if (isValidUtf8(bytes))
        return QString::fromUtf8(bytes);

    // --- Фолбэк: системная ANSI ---
    return QString::fromLocal8Bit(bytes);
}

void ReportGenerator::showTreeRec(const QString& path, const QString& indent, QStringList& outLines) const
{
    QDir dir(path);

    QFileInfoList items = dir.entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
        QDir::NoSort
    );

    // Фильтрация по исключаемым папкам (на любом уровне).
    QFileInfoList filtered;
    filtered.reserve(items.size());
    for (const QFileInfo& it : std::as_const(items))
    {
        if (isUnderExcluded(it))
            continue;

        filtered.push_back(it);
    }

    // Сортировка: папки первыми, затем по имени.
    std::sort(filtered.begin(), filtered.end(), [](const QFileInfo& a, const QFileInfo& b){
        if (a.isDir() != b.isDir())
            return a.isDir() > b.isDir();
        return a.fileName().compare(b.fileName(), Qt::CaseInsensitive) < 0;
    });

    for (int i = 0; i < filtered.size(); ++i)
    {
        const QFileInfo item = filtered.at(i);
        const bool isLast = (i == filtered.size() - 1);

        const QString branch = isLast ? QStringLiteral("└── ") : QStringLiteral("├── ");
        outLines << indent + branch + item.fileName();

        // Важно: чтобы не словить циклы, в симлинки не уходим.
        if (item.isDir() && !item.isSymLink())
        {
            const QString nextIndent = indent + (isLast ? QStringLiteral("    ") : QStringLiteral("│   "));
            showTreeRec(item.absoluteFilePath(), nextIndent, outLines);
        }
    }
}

QString ReportGenerator::runCmdTree(QString* errorOut) const
{
#ifdef Q_OS_WIN
    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);

    // Важно: приводим к Windows-виду, но НЕ добавляем никаких лишних слэшей.
    const QString rootNative = QDir::toNativeSeparators(QDir::cleanPath(m_opt.rootPath));

    /**
     * @details
     *  КРИТИЧНО: используем setNativeArguments(), чтобы Qt не экранировал двойные кавычки как \".
     *  Иначе cmd получает \"C:\...\" и tree видит путь \C:\... (Invalid path).
     */
    const QString cmdLine =
        QStringLiteral("/c chcp 65001>nul & tree \"%1\" /F /A").arg(rootNative);

    proc.setNativeArguments(cmdLine);
    proc.start(QStringLiteral("cmd.exe"));

    if (!proc.waitForStarted())
    {
        if (errorOut) *errorOut = QStringLiteral("Не удалось запустить cmd.exe для выполнения tree.");
        return {};
    }

    proc.waitForFinished(-1);

    // const QByteArray outBytes = proc.readAll();
    // const QString outText = QString::fromUtf8(outBytes);

    const QByteArray outBytes = proc.readAll();

    QString outText;
#ifdef Q_OS_WIN
    outText = decodeOem(outBytes);
#else
    outText = QString::fromUtf8(outBytes);
#endif




    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0)
    {
        if (errorOut)
            *errorOut = QStringLiteral("Команда tree завершилась с ошибкой (exitCode=%1). Вывод: %2")
                            .arg(proc.exitCode())
                            .arg(outText.trimmed());
        return {};
    }

    if (outText.trimmed().isEmpty())
    {
        if (errorOut) *errorOut = QStringLiteral("Команда tree не вернула полезного вывода.");
        return {};
    }

    return outText;
#else
    if (errorOut) *errorOut = QStringLiteral("useCmdTree доступен только в Windows.");
    return {};
#endif
}


void ReportGenerator::collectFilesRec(const QString& dirPath, QVector<QFileInfo>& outFiles) const
{
    QDir dir(dirPath);

    QFileInfoList entries = dir.entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
        QDir::NoSort
    );

    // Сортировка не нужна на этом этапе — отсортируем общий список в generate().
    for (const QFileInfo& it : std::as_const(entries))
    {
        // Пропуск: всё, что находится под исключаемыми папками.
        if (isUnderExcluded(it))
            continue;

        // Пропуск reparse point / symlink (как -Attributes !ReparsePoint в PS).
        if (it.isSymLink())
            continue;

        if (it.isDir())
        {
            collectFilesRec(it.absoluteFilePath(), outFiles);
        }
        else if (it.isFile())
        {
            if (shouldIncludeFile(it))
                outFiles.push_back(it);
        }
    }
}

bool ReportGenerator::isValidUtf8(const QByteArray& data)
{
    const auto u8 = reinterpret_cast<const unsigned char*>(data.constData());
    const int len = data.size();

    int i = 0;
    while (i < len)
    {
        unsigned char c = u8[i];

        // 1-byte ASCII
        if (c <= 0x7F)
        {
            ++i;
            continue;
        }

        int n = 0;      // length of sequence
        uint cp = 0;    // code point

        if ((c & 0xE0) == 0xC0) { n = 2; cp = c & 0x1F; }
        else if ((c & 0xF0) == 0xE0) { n = 3; cp = c & 0x0F; }
        else if ((c & 0xF8) == 0xF0) { n = 4; cp = c & 0x07; }
        else return false;

        if (i + n > len)
            return false;

        for (int k = 1; k < n; ++k)
        {
            unsigned char cc = u8[i + k];
            if ((cc & 0xC0) != 0x80)
                return false;
            cp = (cp << 6) | (cc & 0x3F);
        }

        // Проверка на overlong encoding
        if (n == 2 && cp < 0x80) return false;
        if (n == 3 && cp < 0x800) return false;
        if (n == 4 && cp < 0x10000) return false;

        // Запрет surrogate range
        if (cp >= 0xD800 && cp <= 0xDFFF) return false;

        // Максимум Unicode
        if (cp > 0x10FFFF) return false;

        i += n;
    }
    return true;
}

/**
 * @brief Экранирует строку для вставки в PowerShell-строку в одинарных кавычках.
 * @details В PowerShell одинарная кавычка экранируется удвоением: ' -> ''.
 */
static QString psEscapeSingleQuoted(QString s)
{
    return s.replace('\'', "''");
}

QString ReportGenerator::readDocxText(const QString& docxPath, QString* errorOut) const
{
#ifdef Q_OS_WIN
    QTemporaryDir tmp;
    if (!tmp.isValid())
    {
        if (errorOut) *errorOut = QStringLiteral("Не удалось создать временную папку для распаковки DOCX.");
        return {};
    }

    // 1) DOCX = ZIP, но Expand-Archive в PS5 часто требует расширение .zip
    const QString zipPath = QDir(tmp.path()).filePath(QStringLiteral("docx.zip"));
    QFile::remove(zipPath);
    if (!QFile::copy(docxPath, zipPath))
    {
        if (errorOut) *errorOut = QStringLiteral("Не удалось скопировать DOCX во временный ZIP для распаковки.");
        return {};
    }

    const QString srcZip = psEscapeSingleQuoted(QDir::toNativeSeparators(zipPath));
    const QString dst    = psEscapeSingleQuoted(QDir::toNativeSeparators(tmp.path()));

    /**
     * @details
     *  Устанавливаем UTF-8 для вывода, чтобы ошибки читались нормально.
     *  (Иначе на русской Windows часто будет OEM-кодировка и “кракозябры”.)
     */
    const QString cmd = QStringLiteral(
                            "$ErrorActionPreference='Stop';"
                            "$OutputEncoding=[Console]::OutputEncoding=[System.Text.Encoding]::UTF8;"
                            "Expand-Archive -LiteralPath '%1' -DestinationPath '%2' -Force"
                            ).arg(srcZip, dst);

    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start(QStringLiteral("powershell.exe"),
               QStringList()
                   << QStringLiteral("-NoProfile")
                   << QStringLiteral("-NonInteractive")
                   << QStringLiteral("-ExecutionPolicy") << QStringLiteral("Bypass")
                   << QStringLiteral("-Command") << cmd);

    proc.waitForFinished(-1);

    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0)
    {
        const QByteArray bytes = proc.readAll();
        const QString psOut = QString::fromUtf8(bytes);
        if (errorOut)
            *errorOut = QStringLiteral("Ошибка извлечения DOCX. %1").arg(psOut.trimmed());
        return {};
    }

    // 2) Читаем word/document.xml
    const QString xmlPath = QDir(tmp.path()).filePath(QStringLiteral("word/document.xml"));
    QFile xmlFile(xmlPath);
    if (!xmlFile.open(QIODevice::ReadOnly))
    {
        if (errorOut)
            *errorOut = QStringLiteral("Не найден word/document.xml внутри DOCX или нет доступа: %1").arg(xmlFile.errorString());
        return {};
    }

    // 3) Парсим WordprocessingML и вытаскиваем текст
    QXmlStreamReader xml(&xmlFile);
    QString out;
    out.reserve(4096);

    while (!xml.atEnd())
    {
        xml.readNext();

        if (xml.isStartElement())
        {
            const auto n = xml.name(); // локальное имя без префикса
            if (n == QStringLiteral("t"))
            {
                out += xml.readElementText(QXmlStreamReader::IncludeChildElements);
            }
            else if (n == QStringLiteral("tab"))
            {
                out += QLatin1Char('\t');
            }
            else if (n == QStringLiteral("br") || n == QStringLiteral("cr"))
            {
                out += QLatin1Char('\n');
            }
        }
        else if (xml.isEndElement())
        {
            if (xml.name() == QStringLiteral("p")) // конец параграфа
                out += QLatin1Char('\n');
        }
    }

    if (xml.hasError())
    {
        if (errorOut)
            *errorOut = QStringLiteral("Ошибка XML при чтении DOCX: %1").arg(xml.errorString());
        return {};
    }

    return out.trimmed();
#else
    if (errorOut)
        *errorOut = QStringLiteral("Извлечение текста из .docx сейчас реализовано только для Windows.");
    return {};
#endif
}


QString ReportGenerator::readFileForReport(const QFileInfo& file, QString* errorOut) const
{
    const QString suf = file.suffix().toLower();
    const QString ext = suf.isEmpty() ? QString() : QStringLiteral(".%1").arg(suf);

    if (ext == QStringLiteral(".doc"))
    {
        return QStringLiteral("[Файл .DOC: извлечение текста не реализовано. "
                              "Рекомендуется конвертировать в .DOCX или .TXT.]");
    }

    if (ext == QStringLiteral(".docx"))
    {
        QString err;
        const QString text = readDocxText(file.absoluteFilePath(), &err);
        if (!err.isEmpty())
        {
            if (errorOut) *errorOut = err;
            return {};
        }
        return text;
    }

    if (ext == QStringLiteral(".pdf"))
    {
        QString err;
        const QString text = readPdfText(file.absoluteFilePath(), &err);
        if (!err.isEmpty())
        {
            if (errorOut) *errorOut = err;
            return {};
        }
        return text;
    }

    if (ext == QStringLiteral(".xls"))
    {
        return QStringLiteral("[Файл .XLS: старый бинарный формат Excel. "
                              "Извлечение текста не реализовано. "
                              "Сохраните как .XLSX или .CSV.]");
    }

    if (ext == QStringLiteral(".xlsx") || ext == QStringLiteral(".xlsm"))
    {
        QString err;
        const QString text = readXlsxText(file.absoluteFilePath(), &err);
        if (!err.isEmpty())
        {
            if (errorOut) *errorOut = err;
            return {};
        }
        return text;
    }

    // Обычные текстовые файлы
    return readTextSmart(file.absoluteFilePath(), errorOut);
}

QString ReportGenerator::readPdfText(const QString& pdfPath, QString* errorOut) const
{
    const QString exe = findPdfToTextExe();
    if (exe.isEmpty())
    {
        if (errorOut)
            *errorOut = QStringLiteral(
                "Не найден pdftotext.exe. "
                "Положите Poppler в <папка_приложения>/tools/poppler/pdftotext.exe "
                "или установите pdftotext в систему (PATH).");

        return {};
    }

    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.setProgram(exe);

    // pdftotext [options] PDF-file [text-file], text-file "-" -> stdout
    // -enc UTF-8, -layout см. manpage
    proc.setArguments({ QStringLiteral("-enc"), QStringLiteral("UTF-8"),
                       QStringLiteral("-layout"),
                       pdfPath,
                       QStringLiteral("-") });

    proc.setWorkingDirectory(QFileInfo(exe).absolutePath());
    proc.start();
    if (!proc.waitForStarted())
    {
        if (errorOut)
            *errorOut = QStringLiteral("Не удалось запустить pdftotext: %1").arg(proc.errorString());
        return {};
    }

    proc.waitForFinished(-1);

    const QByteArray outBytes = proc.readAll();
    QString t = QString::fromUtf8(outBytes);

    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0)
    {
        if (errorOut)
            *errorOut = QStringLiteral("pdftotext завершился с ошибкой (exitCode=%1). Вывод: %2")
                            .arg(proc.exitCode())
                            .arg(t.trimmed());
        return {};
    }

    const qint64 maxChars = m_opt.maxOutChars; // 0 = без лимита
    truncateWithNote(t, maxChars,
                     QStringLiteral("[ОБРЕЗАНО: превышен лимит вывода текста]"));

    return t.trimmed();
}



// ---------------- XLSX/XLSM ----------------

static QString xmlAttrByQName(const QXmlStreamAttributes& attrs, const QString& qname)
{
    for (const auto& a : attrs)
    {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        const QString qa = a.qualifiedName().toString();
#else
        const QString qa = a.qualifiedName().toString();
#endif
        if (qa == qname)
            return a.value().toString();
    }
    return {};
}

static int excelColIndexFromCellRef(const QString& ref)
{
    int col = 0;
    int i = 0;
    while (i < ref.size() && ref[i].isLetter())
    {
        const QChar ch = ref[i].toUpper();
        if (ch < QLatin1Char('A') || ch > QLatin1Char('Z'))
            break;
        col = col * 26 + (ch.unicode() - QLatin1Char('A').unicode() + 1);
        ++i;
    }
    return (col > 0) ? (col - 1) : -1; // 0-based
}

static QVector<QString> readSharedStringsXml(const QString& ssPath, QString* errorOut)
{
    QVector<QString> shared;

    QFile f(ssPath);
    if (!f.open(QIODevice::ReadOnly))
        return shared; // sharedStrings может отсутствовать — это нормально

    QXmlStreamReader xml(&f);

    QString cur;
    bool inSi = false;

    while (!xml.atEnd())
    {
        xml.readNext();

        if (xml.isStartElement())
        {
            const auto n = xml.name();
            if (n == QStringLiteral("si"))
            {
                cur.clear();
                inSi = true;
            }
            else if (inSi && n == QStringLiteral("t"))
            {
                cur += xml.readElementText(QXmlStreamReader::IncludeChildElements);
            }
        }
        else if (xml.isEndElement())
        {
            if (xml.name() == QStringLiteral("si"))
            {
                shared.push_back(cur);
                inSi = false;
            }
        }
    }

    if (xml.hasError())
    {
        if (errorOut)
            *errorOut = QStringLiteral("Ошибка XML sharedStrings: %1").arg(xml.errorString());
    }

    return shared;
}

static QString sheetXmlToTsv(const QString& sheetPath,
                             const QVector<QString>& shared,
                             qint64 maxChars,
                             QString* errorOut)
{
    QFile f(sheetPath);
    if (!f.open(QIODevice::ReadOnly))
    {
        if (errorOut)
            *errorOut = QStringLiteral("Не удалось открыть лист XLSX: %1").arg(f.errorString());
        return {};
    }

    QXmlStreamReader xml(&f);
    QString out;
    out.reserve(8192);

    int lastRowNum = 0;
    int currentRowNum = 0;

    QMap<int, QString> rowCells;
    int maxCol = -1;

    bool inCell = false;
    QString cellRef;
    QString cellType;
    QString cellValue;

    auto flushRow = [&]() -> bool {
        if (currentRowNum <= 0)
            return true;

        if (maxCol >= 0)
        {
            QStringList cols;
            cols.resize(maxCol + 1);
            for (auto it = rowCells.begin(); it != rowCells.end(); ++it)
            {
                const int c = it.key();
                if (c >= 0 && c < cols.size())
                    cols[c] = it.value();
            }

            // Пишем: номер строки + табличные значения
            out += QString::number(currentRowNum);
            out += QLatin1Char('\t');
            out += cols.join(QStringLiteral("\t"));
        }
        else
        {
            out += QString::number(currentRowNum);
        }

        out += QLatin1Char('\n');

        if (maxChars > 0 && out.size() > maxChars)
        {
            truncateWithNote(out, maxChars, QStringLiteral("[ОБРЕЗАНО: слишком много данных]"));
            return false;
        }

        return true;
    };

    while (!xml.atEnd())
    {
        xml.readNext();

        if (xml.isStartElement())
        {
            const auto n = xml.name();

            if (n == QStringLiteral("row"))
            {
                bool ok = false;
                currentRowNum = xml.attributes().value(QStringLiteral("r")).toInt(&ok);
                if (!ok || currentRowNum <= 0)
                    currentRowNum = lastRowNum + 1;
                lastRowNum = currentRowNum;

                rowCells.clear();
                maxCol = -1;
            }
            else if (n == QStringLiteral("c"))
            {
                inCell = true;
                cellRef = xml.attributes().value(QStringLiteral("r")).toString();
                cellType = xml.attributes().value(QStringLiteral("t")).toString();
                cellValue.clear();
            }
            else if (inCell && n == QStringLiteral("v"))
            {
                cellValue = xml.readElementText(QXmlStreamReader::IncludeChildElements);
            }
            else if (inCell && cellType == QStringLiteral("inlineStr") && n == QStringLiteral("t"))
            {
                cellValue += xml.readElementText(QXmlStreamReader::IncludeChildElements);
            }
        }
        else if (xml.isEndElement())
        {
            const auto n = xml.name();

            if (n == QStringLiteral("c") && inCell)
            {
                const int col = excelColIndexFromCellRef(cellRef);

                QString val;
                if (cellType == QStringLiteral("s"))
                {
                    bool ok = false;
                    const int idx = cellValue.toInt(&ok);
                    if (ok && idx >= 0 && idx < shared.size())
                        val = shared[idx];
                    else
                        val = QStringLiteral("[bad sharedString index: %1]").arg(cellValue);
                }
                else if (cellType == QStringLiteral("b"))
                {
                    val = (cellValue == QStringLiteral("1")) ? QStringLiteral("TRUE") : QStringLiteral("FALSE");
                }
                else
                {
                    val = cellValue;
                }

                if (col >= 0)
                {
                    rowCells[col] = val;
                    maxCol = std::max(maxCol, col);
                }

                inCell = false;
            }
            else if (n == QStringLiteral("row"))
            {
                if (!flushRow())
                    break;
                currentRowNum = 0;
            }
        }
    }

    if (xml.hasError())
    {
        if (errorOut)
            *errorOut = QStringLiteral("Ошибка XML листа XLSX: %1").arg(xml.errorString());
        return {};
    }

    return out.trimmed();
}

QString ReportGenerator::readXlsxText(const QString& xlsxPath, QString* errorOut) const
{
#ifdef Q_OS_WIN
    QTemporaryDir tmp;
    if (!tmp.isValid())
    {
        if (errorOut) *errorOut = QStringLiteral("Не удалось создать временную папку для распаковки XLSX.");
        return {};
    }

    // XLSX/XLSM = ZIP, но Expand-Archive любит .zip
    const QString zipPath = QDir(tmp.path()).filePath(QStringLiteral("xlsx.zip"));
    QFile::remove(zipPath);
    if (!QFile::copy(xlsxPath, zipPath))
    {
        if (errorOut) *errorOut = QStringLiteral("Не удалось скопировать XLSX во временный ZIP для распаковки.");
        return {};
    }

    const QString srcZip = psEscapeSingleQuoted(QDir::toNativeSeparators(zipPath));
    const QString dst    = psEscapeSingleQuoted(QDir::toNativeSeparators(tmp.path()));

    const QString cmd = QStringLiteral(
                            "$ErrorActionPreference='Stop';"
                            "$OutputEncoding=[Console]::OutputEncoding=[System.Text.Encoding]::UTF8;"
                            "Expand-Archive -LiteralPath '%1' -DestinationPath '%2' -Force"
                            ).arg(srcZip, dst);

    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start(QStringLiteral("powershell.exe"),
               QStringList()
                   << QStringLiteral("-NoProfile")
                   << QStringLiteral("-NonInteractive")
                   << QStringLiteral("-ExecutionPolicy") << QStringLiteral("Bypass")
                   << QStringLiteral("-Command") << cmd);

    proc.waitForFinished(-1);

    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0)
    {
        const QString out = QString::fromUtf8(proc.readAll()).trimmed();
        if (errorOut)
            *errorOut = QStringLiteral("Ошибка извлечения XLSX (Expand-Archive): %1").arg(out);
        return {};
    }

    const QString xlDir = QDir(tmp.path()).filePath(QStringLiteral("xl"));
    const QString ssPath = QDir(xlDir).filePath(QStringLiteral("sharedStrings.xml"));

    QString ssErr;
    const QVector<QString> shared = readSharedStringsXml(ssPath, &ssErr);
    // ssErr не считаем фатальным — sharedStrings может отсутствовать

    // workbook rels: xl/_rels/workbook.xml.rels
    QHash<QString, QString> rel;
    {
        const QString relsPath = QDir(xlDir).filePath(QStringLiteral("_rels/workbook.xml.rels"));
        QFile f(relsPath);
        if (f.open(QIODevice::ReadOnly))
        {
            QXmlStreamReader xml(&f);
            while (!xml.atEnd())
            {
                xml.readNext();
                if (xml.isStartElement() && xml.name() == QStringLiteral("Relationship"))
                {
                    const QString id = xml.attributes().value(QStringLiteral("Id")).toString();
                    const QString target = xml.attributes().value(QStringLiteral("Target")).toString();
                    if (!id.isEmpty() && !target.isEmpty())
                        rel.insert(id, target);
                }
            }
        }
    }

    struct Sheet { QString name; QString path; };
    QVector<Sheet> sheets;

    // workbook: xl/workbook.xml
    {
        const QString wbPath = QDir(xlDir).filePath(QStringLiteral("workbook.xml"));
        QFile f(wbPath);
        if (f.open(QIODevice::ReadOnly))
        {
            QXmlStreamReader xml(&f);
            while (!xml.atEnd())
            {
                xml.readNext();
                if (xml.isStartElement() && xml.name() == QStringLiteral("sheet"))
                {
                    const QString name = xml.attributes().value(QStringLiteral("name")).toString();
                    const QString rid = xmlAttrByQName(xml.attributes(), QStringLiteral("r:id"));
                    const QString target = rel.value(rid);

                    // target обычно "worksheets/sheet1.xml"
                    QString sheetPath;
                    if (!target.isEmpty())
                        sheetPath = QDir(xlDir).filePath(target);

                    if (!sheetPath.isEmpty() && QFileInfo::exists(sheetPath))
                    {
                        sheets.push_back({ name.isEmpty() ? QFileInfo(sheetPath).baseName() : name, sheetPath });
                    }
                }
            }
        }
    }

    // fallback: если workbook не распарсили — берём все xl/worksheets/*.xml
    if (sheets.isEmpty())
    {
        QDir wsDir(QDir(xlDir).filePath(QStringLiteral("worksheets")));
        const QStringList files = wsDir.entryList(QStringList() << QStringLiteral("*.xml"),
                                                  QDir::Files, QDir::Name);
        for (const QString& fn : files)
            sheets.push_back({ fn, wsDir.filePath(fn) });
    }

    if (sheets.isEmpty())
    {
        if (errorOut) *errorOut = QStringLiteral("В XLSX не найдены листы (worksheets).");
        return {};
    }

    // Ограничение на объём текста (в символах) — чтобы не взорваться на “маленьком, но супер-упакованном” xlsx.
   // const qint64 maxChars = (m_opt.maxBytes > 0) ? m_opt.maxBytes : (1024 * 1024);
    const qint64 maxChars = m_opt.maxOutChars; // 0 = без лимита

    QString out;
    out.reserve(8192);

    out += QStringLiteral("[XLSX] %1\n").arg(QFileInfo(xlsxPath).fileName());
    if (!ssErr.isEmpty())
        out += QStringLiteral("[предупреждение sharedStrings] %1\n").arg(ssErr);

    for (const Sheet& sh : sheets)
    {
        out += QStringLiteral("\n----- SHEET: %1 -----\n").arg(sh.name);

        QString sheetErr;
        const QString tsv = sheetXmlToTsv(sh.path, shared, maxChars, &sheetErr);

        if (!sheetErr.isEmpty())
        {
            out += QStringLiteral("[ОШИБКА ЛИСТА: %1]\n").arg(sheetErr);
            continue;
        }

        out += tsv;
        out += QLatin1Char('\n');

        if (maxChars > 0 && out.size() > maxChars)
        {
            truncateWithNote(out, maxChars,
                             QStringLiteral("[ОБЩЕЕ ОБРЕЗАНО: слишком много данных]"));
            break;
        }

    }

    return out.trimmed();

#else
    if (errorOut)
        *errorOut = QStringLiteral("Извлечение текста из XLSX сейчас реализовано только для Windows.");
    return {};
#endif
}


QString ReportGenerator::findPdfToTextExe() const
{
#ifdef Q_OS_WIN
    const QDir appDir(QCoreApplication::applicationDirPath());

    // 1) Вариант "положили прямо рядом с ContextMaker.exe" (не лучший, но пусть будет)
    const QString flat = appDir.filePath(QStringLiteral("pdftotext.exe"));
    if (QFileInfo::exists(flat))
        return flat;

    // 2) РЕКОМЕНДУЕМЫЙ вариант для деплоя:
    // <рядом с exe>/tools/poppler/pdftotext.exe
    const QString toolsPoppler = appDir.filePath(QStringLiteral("tools/poppler/pdftotext.exe"));
    if (QFileInfo::exists(toolsPoppler))
        return toolsPoppler;

    // 3) Альтернатива: <рядом с exe>/poppler/pdftotext.exe
    const QString poppler = appDir.filePath(QStringLiteral("poppler/pdftotext.exe"));
    if (QFileInfo::exists(poppler))
        return poppler;
#endif

    // 4) Если пользователь установил pdftotext в систему и добавил в PATH
    const QString inPath = QStandardPaths::findExecutable(QStringLiteral("pdftotext"));
    if (!inPath.isEmpty())
        return inPath;

    return {};
}




