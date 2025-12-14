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
    for (const QString& ext : m_opt.includeExt)
    {
        QString e = ext.trimmed();
        if (!e.isEmpty() && !e.startsWith('.'))
            e.prepend('.');
        m_includeSet.insert(e.toLower());
    }

    // Нормализуем исключаемые папки (сравнение по имени папки).
    for (const QString& name : m_opt.excludeDirNames)
        m_excludeSet.insert(name.trimmed());
}

QString ReportGenerator::generate(QString* errorOut) const
{
    if (m_opt.rootPath.trimmed().isEmpty())
    {
        if (errorOut) *errorOut = QStringLiteral("Не задан корневой каталог.");
        return {};
    }

    const QString root = QDir::cleanPath(m_opt.rootPath);

    if (!QFileInfo(root).exists() || !QFileInfo(root).isDir())
    {
        if (errorOut) *errorOut = QStringLiteral("Каталог не найден: %1").arg(root);
        return {};
    }

    QStringList lines;
    lines << QStringLiteral("# Отчёт по каталогу: %1").arg(root);
    lines << QStringLiteral("## 1. Дерево каталогов и файлов");

    // Если корневая папка сама в списке исключений — отчёт будет пустым (как в PS-скрипте).
    const QString rootName = QFileInfo(root).fileName();
    const bool rootExcluded = m_excludeSet.contains(rootName);

#ifdef Q_OS_WIN
    const bool onWindows = true;
#else
    const bool onWindows = false;
#endif

    if (!rootExcluded)
    {
        if (m_opt.useCmdTree && onWindows)
        {
            QString treeErr;
            const QString treeOut = runCmdTree(&treeErr);
            if (!treeOut.isEmpty())
                lines << treeOut.trimmed();
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
            QStringList treeLines;
            showTreeRec(root, QString(), treeLines);
            lines << treeLines;
        }
    }

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

        for (const QFileInfo& f : files)
        {
            const QString abs = f.absoluteFilePath();
            QString rel = rootDir.relativeFilePath(abs);
            rel = QDir::toNativeSeparators(rel);

            lines << QStringLiteral("```text");
            lines << QStringLiteral("----- BEGIN FILE: %1 [%2 bytes] ----").arg(rel).arg(f.size());

            QString readErr;
            const QString content = readTextSmart(abs, &readErr);
            if (!readErr.isEmpty())
                lines << QStringLiteral("[ОШИБКА ЧТЕНИЯ: %1]").arg(readErr);
            else
                lines << content;

            lines << QStringLiteral("----- END FILE:   %1 ----").arg(rel);
            lines << QStringLiteral("```");
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
        const QString name = dirNameFromPath(dir.absolutePath());
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
        const int n = payload.size() / 4;
        QVector<uint> cps;
        cps.reserve(n);
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
    }

    // UTF-32 BE BOM: 00 00 FE FF
    if (startsWith({0x00, 0x00, 0xFE, 0xFF}))
    {
        const QByteArray payload = bytes.mid(4);
        const int n = payload.size() / 4;
        QVector<uint> cps;
        cps.reserve(n);
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
    }

    // UTF-16 LE BOM: FF FE
    if (startsWith({0xFF, 0xFE}))
    {
        const QByteArray payload = bytes.mid(2);
        QVector<ushort> u16;
        u16.reserve(payload.size() / 2);
        for (int i = 0; i + 1 < payload.size(); i += 2)
        {
            const ushort cu = (ushort)((unsigned char)payload[i] | ((unsigned char)payload[i + 1] << 8));
            u16.push_back(cu);
        }
        return QString::fromUtf16(u16.constData(), u16.size());
    }

    // UTF-16 BE BOM: FE FF
    if (startsWith({0xFE, 0xFF}))
    {
        const QByteArray payload = bytes.mid(2);
        QVector<ushort> u16;
        u16.reserve(payload.size() / 2);
        for (int i = 0; i + 1 < payload.size(); i += 2)
        {
            const ushort cu = (ushort)(((unsigned char)payload[i] << 8) | (unsigned char)payload[i + 1]);
            u16.push_back(cu);
        }
        return QString::fromUtf16(u16.constData(), u16.size());
    }

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
    for (const QFileInfo& it : items)
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

    // Важно: принудительно включаем UTF-8 в cmd (chcp 65001), как в PowerShell-скрипте.
    const QString rootNative = QDir::toNativeSeparators(QDir::cleanPath(m_opt.rootPath));
    const QString cmdLine = QStringLiteral("chcp 65001>nul & tree \"%1\" /F /A").arg(rootNative);

    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start(QStringLiteral("cmd"), QStringList() << QStringLiteral("/c") << cmdLine);

    if (!proc.waitForStarted())
    {
        if (errorOut) *errorOut = QStringLiteral("Не удалось запустить cmd.exe для выполнения tree.");
        return {};
    }

    proc.waitForFinished(-1);

    const QByteArray out = proc.readAll();
    if (out.isEmpty())
    {
        if (errorOut) *errorOut = QStringLiteral("Команда tree не вернула вывод.");
        return {};
    }

    // Вывод у нас ASCII/UTF-8, т.к. мы сделали chcp 65001.
    return QString::fromUtf8(out);

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
    for (const QFileInfo& it : entries)
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
