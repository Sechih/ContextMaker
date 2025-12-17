/** \brief Скрипт QtIFW: создаёт ярлыки после копирования файлов
 *  \details @TargetDir@ — папка, куда установлено приложение
 */
function Component() {}

Component.prototype.createOperations = function() {
    component.createOperations();

    /** \brief Путь к exe внутри bin */
    var exe = "@TargetDir@\\bin\\ContextMaker.exe"; // поправьте имя exe если другое

    /** \brief Ярлык в меню Пуск */
    component.addOperation("CreateShortcut",
        exe,
        "@StartMenuDir@\\ContextMaker.lnk",
        "workingDirectory=@TargetDir@\\bin");

    /** \brief Ярлык на рабочем столе */
    component.addOperation("CreateShortcut",
        exe,
        "@DesktopDir@\\ContextMaker.lnk",
        "workingDirectory=@TargetDir@\\bin");
};
