# TreeContentsReport (Qt)

Приложение повторяет логику PowerShell-скрипта `Export-TreeWithContents.ps1`:
- Секция 1: дерево каталогов/файлов (Unicode ├──/└──) или (Windows) через `tree /F /A`
- Секция 2: содержимое выбранных текстовых файлов по расширениям + лимит размера

## UI
- **Открыть…** — выбрать корневой каталог
- **Собрать отчёт** — сформировать отчёт и показать в `QTextEdit`
- **Сохранить…** — сохранить содержимое `QTextEdit` в `report.md` (UTF‑8 с BOM)
- ПКМ по `QTextEdit` — **Копировать**

## Сборка (CMake)
```bash
cmake -S . -B build
cmake --build build --config Release
```

## Сборка (qmake)
```bash
qmake
make   # или mingw32-make / nmake в зависимости от окружения
```
