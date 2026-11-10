set(MAIN_SOURCES
    "main.c"
    "app_audio.c"
    "app_bus.c"
    "app_httpd.c"
    "app_player.c"
    "app_settings.c"
    "app_store.c"
    "app_time.c"
    "app_wifi.c"
    "ui/launcher.c"
    "ui/icons.c"
    "ui/ui_music.c"
    "ui/ui_nav.c"
    "ui/ui_settings.c"
    "ui/ui_theme.c"
    "fonts/ui_font_16.c"
    "fonts/ui_font_24.c"
    "fonts/ui_font_32.c"
    "fonts/ui_font_cjk24.c"
)

idf_component_register(
    SRCS ${MAIN_SOURCES}
    INCLUDE_DIRS "." "ui" "fonts"
)
