add_executable(kspotify
    main.cpp
    mainwindow.cpp
    mainwindow.h
)

target_link_libraries(kspotify PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::Widgets
    Qt6::WebEngineWidgets
    KF6::ColorScheme
    KF6::CoreAddons
    KF6::I18n
    KF6::XmlGui
    KF6::Crash
    KF6::DBusAddons
    KF6::Notifications
    KF6::StatusNotifierItem
)

install(TARGETS kspotify DESTINATION ${KDE_INSTALL_BINDIR})
install(FILES kspotifyui.rc DESTINATION ${KDE_INSTALL_KXMLGUIDIR}/kspotify)
