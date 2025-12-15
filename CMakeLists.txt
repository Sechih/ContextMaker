cmake_minimum_required(VERSION 3.16)

project(ContextMaker VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets LinguistTools)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets LinguistTools)

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

target_link_libraries(ContextMaker PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)

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

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(ContextMaker)
endif()


# ===================================================================
#  УНИВЕРСАЛЬНЫЙ БЛОК: install + deploy для Qt‑приложения
#
#  Предполагается:
#    - цель приложения называется ${PROJECT_NAME}
#    - Qt 6 (для Qt 5 блок с qt_generate_deploy_app_script просто не сработает)
#    - qt_finalize_executable(${PROJECT_NAME}) больше НИГДЕ в CMakeLists.txt нет
# ===================================================================

include(GNUInstallDirs)

#/** \brief Куда ставим exe и библиотеки при `cmake --install`. */
install(TARGETS ${PROJECT_NAME}
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

#/** \brief Qt 6: генерация скрипта деплоя (windeployqt/macdeployqt и т.п.). */
if (QT_VERSION_MAJOR EQUAL 6)
    qt_generate_deploy_app_script(
        TARGET ${PROJECT_NAME}
        OUTPUT_SCRIPT qt_deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR
    )
    #/** \brief Подключаем скрипт деплоя в install. */
    install(SCRIPT ${qt_deploy_script})

    #/** \brief ВАЖНО: финализируем исполняемый файл ТОЛЬКО здесь. */
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

