#include "mainwindow.h"

#include <QApplication>
#include <QLocale>
#include <QTranslator>
#include <QStyleFactory>
#include <QSettings>
#include <QPalette>
#include <QStyle>




/**
 * @brief Проверяет, включён ли тёмный режим приложений в Windows (AppsUseLightTheme=0).
 */
static bool isWindowsAppDarkMode()
{
#ifdef Q_OS_WIN
    QSettings s(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"),
                QSettings::NativeFormat);
    const QVariant v = s.value(QStringLiteral("AppsUseLightTheme"));
    if (!v.isValid())
        return false; // если ключа нет — считаем светлый режим
    return (v.toInt() == 0);
#else
    return false;
#endif
}

/**
 * @brief Тёмная палитра (универсальная, выглядит адекватно в большинстве случаев).
 * @note Это не “идеально один-в-один как Windows”, но очень похоже по смыслу.
 */
static QPalette makeDarkPalette()
{
    QPalette p;
    p.setColor(QPalette::Window, QColor(32, 32, 32));
    p.setColor(QPalette::WindowText, QColor(220, 220, 220));
    p.setColor(QPalette::Base, QColor(24, 24, 24));
    p.setColor(QPalette::AlternateBase, QColor(32, 32, 32));
    p.setColor(QPalette::ToolTipBase, QColor(255, 255, 255));
    p.setColor(QPalette::ToolTipText, QColor(0, 0, 0));
    p.setColor(QPalette::Text, QColor(220, 220, 220));
    p.setColor(QPalette::Button, QColor(45, 45, 45));
    p.setColor(QPalette::ButtonText, QColor(220, 220, 220));
    p.setColor(QPalette::BrightText, QColor(255, 0, 0));
    p.setColor(QPalette::Highlight, QColor(80, 120, 200));
    p.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
    return p;
}

/**
 * @brief Применяет “системную” тему Windows: нативный стиль + тёмный режим, если включён.
 */
static void applyWindowsTheme(QApplication& app)
{
#ifdef Q_OS_WIN
    // Гарантируем нативный стиль Windows (если доступен).
    if (QStyleFactory::keys().contains(QStringLiteral("windowsvista"), Qt::CaseInsensitive))
        app.setStyle(QStyleFactory::create(QStringLiteral("windowsvista")));

    // Если в системе включен dark mode для приложений — применяем тёмную палитру.
    if (isWindowsAppDarkMode())
    {
        // Вариант 1 (нативно): оставить windowsvista и просто палитру.
        // app.setPalette(makeDarkPalette());

        // Вариант 2 (самый стабильный для dark): Fusion + тёмная палитра.
        app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
        app.setPalette(makeDarkPalette());
    }
    else
    {
        // Светлый режим — стандартная палитра текущего стиля.
        app.setPalette(app.style()->standardPalette());
    }
#else
    Q_UNUSED(app);
#endif
}


















int main(int argc, char *argv[])
{
    QApplication a(argc, argv);


    applyWindowsTheme(a);
    a.setWindowIcon(QIcon(":/icons/icons/documents_papers_sheets_icon_187064.ico"));


    QTranslator translator;
    const QStringList uiLanguages = QLocale::system().uiLanguages();
    for (const QString &locale : uiLanguages) {
        const QString baseName = "ContextMaker_" + QLocale(locale).name();
        if (translator.load(":/i18n/" + baseName)) {
            a.installTranslator(&translator);
            break;
        }
    }
    MainWindow w;
    w.show();
    return a.exec();
}
