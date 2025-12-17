cmake_minimum_required(VERSION 3.16)

project(ContextMaker VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets LinguistTools Concurrent)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets LinguistTools Concurrent)

set(TS_FILES ContextMaker_ru_RU.ts)

set(PROJECT_SOURCES
        main.cpp
        mainwindow.cpp
        mainwindow.h
        mainwindow.ui
        reportgenerator.h
        reportgenerator.cpp
        ${TS_FILES}
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(ContextMaker
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
        resources/app_resources.qrc
        appicon.rc


    )


# Define target properties for Android with Qt 6 as:
#    set_property(TARGET ContextMaker APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
#                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
# For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation

    qt_create_translation(QM_FILES ${CMAKE_SOURCE_DIR} ${TS_FILES})
else()
    if(ANDROID)
        add_library(ContextMaker SHARED
            ${PROJECT_SOURCES}
        )
# Define properties for Android with Qt 5 after find_package() calls as:
#    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    else()
        add_executable(ContextMaker
            ${PROJECT_SOURCES}
        )
    endif()

    qt5_create_translation(QM_FILES ${CMAKE_SOURCE_DIR} ${TS_FILES})
endif()

target_link_libraries(ContextMaker PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Concurrent
)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
if(${QT_VERSION} VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.ContextMaker)
endif()
set_target_properties(ContextMaker PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS ContextMaker
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)


if (QT_VERSION_MAJOR EQUAL 6)
    # Qt 6: генерируем скрипт деплоя (windeployqt/macdeployqt и т.п.)
    qt_generate_deploy_app_script(
        TARGET ${PROJECT_NAME}
        OUTPUT_SCRIPT qt_deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR
    )

    # Подключаем скрипт деплоя в install
    install(SCRIPT ${qt_deploy_script})

    # Финализируем исполняемый файл РОВНО один раз
    qt_finalize_executable(${PROJECT_NAME})
endif()



# ===================================================================
#/** \brief Удобная цель deploy для Windows. */
#/** \details Вызывает `cmake --install` с нужным префиксом. */
# ===================================================================
if (WIN32 AND QT_VERSION_MAJOR EQUAL 6)
    #/** \brief Куда складывать готовый деплой (по умолчанию: <корень_проекта>/deploy,
    # *         т.е. рядом с каталогом build при out-of-source сборке). */
    set(APP_DEPLOY_PREFIX "${CMAKE_SOURCE_DIR}/deploy"
        CACHE PATH "Install / deploy prefix")

    #/** \brief Цель deploy, которую будем вызывать из Qt Creator / консоли. */
    add_custom_target(deploy
        COMMAND "${CMAKE_COMMAND}" --install "${CMAKE_BINARY_DIR}"
                --config $<CONFIG>
                --prefix "${APP_DEPLOY_PREFIX}"
        COMMENT "Deploying ${PROJECT_NAME} to ${APP_DEPLOY_PREFIX}"
    )

    #/** \brief Перед деплоем обязательно собираем само приложение. */
    add_dependencies(deploy ${PROJECT_NAME})
endif()


#/** \brief Копируем папку tools рядом с exe (в bin), чтобы работали внешние утилиты (pdftotext и т.п.). */
if (WIN32)
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/tools"
            DESTINATION "${CMAKE_INSTALL_BINDIR}"
            OPTIONAL)
endif()



# ===================================================================
# /** \brief QtIFW: оффлайн-инсталлятор из папки deploy. */
# /** \details Берёт готовый deploy, собирает .exe установщик через QtIFW. */
# ===================================================================
if(WIN32 AND QT_VERSION_MAJOR EQUAL 6)
    #/** \brief Путь к Qt Installer Framework (bin с binarycreator.exe). */
    set(IFW_BIN "C:/Tools/Qt/Tools/QtInstallerFramework/4.10/bin"
        CACHE PATH "Path to QtIFW bin")

    set(IFW_BINARYCREATOR "${IFW_BIN}/binarycreator.exe")
    if(NOT EXISTS "${IFW_BINARYCREATOR}")
        message(FATAL_ERROR "Не найден QtIFW: ${IFW_BINARYCREATOR}")
    endif()

    #/** \brief Корень шаблонов инсталлятора в репозитории. */
    set(INSTALLER_ROOT   "${CMAKE_SOURCE_DIR}/installer")
    set(INSTALLER_CONFIG "${INSTALLER_ROOT}/config/config.xml")

    if(NOT EXISTS "${INSTALLER_CONFIG}")
        message(FATAL_ERROR "Не найден файл конфигурации инсталлятора: ${INSTALLER_CONFIG}")
    endif()

    #/** \brief Идентификатор пакета (совпадает с папкой и <Name> в package.xml).
    # *  По умолчанию: com.<project_name_lower>.app
    # */
    string(TOLOWER "${PROJECT_NAME}" PROJECT_NAME_LC)
    set(PKG_ID "com.${PROJECT_NAME_LC}.app" CACHE STRING "QtIFW package id")

    # Пример: структура installer/packages/com.example.app/meta/...
    set(PKG_META_SRC "${INSTALLER_ROOT}/packages/${PKG_ID}/meta")

    if(NOT EXISTS "${PKG_META_SRC}/package.xml")
        message(FATAL_ERROR "Не найден файл ${PKG_META_SRC}/package.xml (meta пакета)")
    endif()

    #/** \brief Рабочая папка QtIFW в каталоге сборки. */
    set(IFW_WORK          "${CMAKE_BINARY_DIR}/ifw")
    set(IFW_WORK_CONFIG   "${IFW_WORK}/config/config.xml")
    set(IFW_WORK_PACKAGES "${IFW_WORK}/packages")
    set(PKG_DIR_WORK      "${IFW_WORK_PACKAGES}/${PKG_ID}")
    set(PKG_META_WORK     "${PKG_DIR_WORK}/meta")
    set(PKG_DATA_WORK     "${PKG_DIR_WORK}/data")

    #/** \brief Папка с готовым деплоем (цель deploy кладёт файлы именно сюда). */
    if(NOT DEFINED APP_DEPLOY_PREFIX)
        # По умолчанию: deploy в корне проекта, как в шаблоне deploy
        set(APP_DEPLOY_PREFIX "${CMAKE_SOURCE_DIR}/deploy")
    endif()

    #/** \brief Имя итогового установщика. */
    set(INSTALLER_OUT "${CMAKE_BINARY_DIR}/${PROJECT_NAME}-${PROJECT_VERSION}-setup.exe")

    #/** \brief Цель installer: собрать оффлайн-установщик QtIFW. */
    add_custom_target(installer
        # сначала обновляем deploy (внутри собирается приложение и делается cmake --install)
        DEPENDS deploy

        # 1) Подготовить рабочую структуру IFW
        COMMAND "${CMAKE_COMMAND}" -E remove_directory "${IFW_WORK}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${PKG_META_WORK}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${PKG_DATA_WORK}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${IFW_WORK}/config"

        # 2) Скопировать meta и config
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${PKG_META_SRC}" "${PKG_META_WORK}"
        COMMAND "${CMAKE_COMMAND}" -E copy "${INSTALLER_CONFIG}" "${IFW_WORK_CONFIG}"

        # 3) Payload: всё из deploy
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${APP_DEPLOY_PREFIX}" "${PKG_DATA_WORK}"

        # 4) Сборка оффлайн-установщика
        COMMAND "${IFW_BINARYCREATOR}"
                -c "${IFW_WORK_CONFIG}"
                -p "${IFW_WORK_PACKAGES}"
                "${INSTALLER_OUT}"

        COMMENT "QtIFW: упаковка ${APP_DEPLOY_PREFIX} → ${INSTALLER_OUT}"
        VERBATIM
    )
endif()
