/**
 * @file reportgenerator.h
 * @brief Генератор отчёта: дерево каталогов + содержимое текстовых файлов.
 */

#pragma once

#include <QString>
#include <QStringList>
#include <QSet>
#include <QFileInfo>
#include <QVector>

/**
 * @brief Класс, который повторяет логику PowerShell-скрипта Export-TreeWithContents.ps1,
 *        но возвращает отчёт как строку (для отображения в QTextEdit и/или сохранения).
 */
class ReportGenerator
{
public:
    /**
     * @brief Параметры генерации отчёта.
     */
    struct Options
    {
        QString rootPath;
        QStringList includeExt;
        QStringList excludeDirNames;
        qint64 maxBytes = 1024 * 1024;
        qint64 maxOutChars = 1024 * 1024;   // лимит текста, вставляемого в отчёт (символы). 0 = без лимита
        bool useCmdTree = false;
        bool treeOnly = false; // Если true — генерируем только дерево, без секции 2


        /**
     * @brief Поведение для файлов БЕЗ BOM.
     */
        enum class NoBomEncodingMode
        {
            AutoUtf8ThenAnsi,  ///< Как сейчас: strict UTF‑8, иначе fallback ANSI (system)
            ForceAnsi          ///< Игнорировать попытку UTF‑8 и читать как ANSI (system)
        };

        NoBomEncodingMode noBomEncodingMode = NoBomEncodingMode::AutoUtf8ThenAnsi;
    };


    explicit ReportGenerator(const Options& opt);

    /**
     * @brief Сформировать полный отчёт.
     * @param errorOut (опционально) строка с сообщением об ошибке.
     * @return Полный отчёт в формате Markdown (как в исходном скрипте).
     */
    QString generate(QString* errorOut = nullptr) const;

private:
    Options m_opt;
    QSet<QString> m_includeSet;   ///< Быстрый набор расширений (в нижнем регистре).
    QSet<QString> m_excludeSet;   ///< Быстрый набор исключаемых папок (как имена).

    /**
     * @brief Проверка: папка/файл находится внутри исключаемой директории (на любом уровне).
     * @param item Информация о файле/папке.
     * @return true если объект находится в одной из excluded папок.
     */
    bool isUnderExcluded(const QFileInfo& item) const;

    /**
     * @brief Проверка: можно ли читать содержимое файла.
     * @param file Информация о файле.
     * @return true если расширение разрешено и размер <= maxBytes.
     */
    bool shouldIncludeFile(const QFileInfo& file) const;

    /**
     * @brief Считывание текста с простым авто-определением кодировки.
     * @details
     *  1) BOM: UTF-8 / UTF-16 LE/BE / UTF-32 LE/BE
     *  2) Без BOM: строгая проверка UTF-8 (валидность последовательностей).
     *  3) Фолбэк: системная ANSI (QString::fromLocal8Bit).
     */
    QString readTextSmart(const QString& path, QString* errorOut = nullptr) const;

    /**
     * @brief Сформировать дерево каталога с псевдографикой (Unicode).
     * @param path Путь к каталогу.
     * @param indent Текущий префикс отступов (служебный).
     * @param outLines Выходные строки дерева.
     */
    void showTreeRec(const QString& path, const QString& indent, QStringList& outLines) const;

    /**
     * @brief Запустить "tree /F /A" через cmd (только Windows) и вернуть вывод.
     * @param errorOut (опционально) сообщение об ошибке.
     */
    QString runCmdTree(QString* errorOut = nullptr) const;

    /**
     * @brief Собрать список файлов (с учётом исключений), чтобы потом вывести содержимое.
     * @param dirPath Текущая директория.
     * @param outFiles Список файлов для чтения.
     */
    void collectFilesRec(const QString& dirPath, QVector<QFileInfo>& outFiles) const;

    /**
     * @brief Проверка UTF-8 на валидность (строгая, без "замен").
     * @param data Байты.
     */
    static bool isValidUtf8(const QByteArray& data);
    /**
     * @brief Читает файл “по-умному” для вставки в отчёт.
     * @details
     *  - .docx: попытка извлечь текст
     *  - .doc: текст не извлекаем (сообщение)
     *  - остальное: readTextSmart()
     */
    QString readFileForReport(const QFileInfo& file, QString* errorOut = nullptr) const;

    /**
     * @brief Извлекает текст из DOCX.
     * @details Реализация для Windows: распаковка через PowerShell Expand-Archive
     *          и чтение word/document.xml.
     */
    QString readDocxText(const QString& docxPath, QString* errorOut = nullptr) const;

    /**
     * @brief Извлекает текст из PDF через внешнюю утилиту pdftotext (Poppler).
     * @note  Нужен pdftotext.exe рядом с приложением или в PATH.
     */
    QString readPdfText(const QString& pdfPath, QString* errorOut = nullptr) const;

    /**
     * @brief Извлекает текст из XLSX/XLSM (OpenXML) через распаковку и парсинг XML.
     * @note  Реализация через PowerShell Expand-Archive (Windows).
     */
    QString readXlsxText(const QString& xlsxPath, QString* errorOut = nullptr) const;


    QString findPdfToTextExe() const;

};
