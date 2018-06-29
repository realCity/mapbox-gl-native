add_library(nunicode STATIC
    vendor/nunicode/src/libnu/ducet.c
    vendor/nunicode/src/libnu/strcoll.c
    vendor/nunicode/src/libnu/strings.c
    vendor/nunicode/src/libnu/tolower.c
    vendor/nunicode/src/libnu/tounaccent.c
    vendor/nunicode/src/libnu/toupper.c
    vendor/nunicode/src/libnu/tofold.c
    vendor/nunicode/src/libnu/utf8.c
)

target_include_directories(nunicode
    PUBLIC vendor/nunicode/include
)

target_compile_definitions(nunicode
    PUBLIC "-DNU_WITH_UTF8"
    PUBLIC "-DNU_WITH_Z_COLLATION"
    PUBLIC "-DNU_WITH_CASEMAP"
    PUBLIC "-DNU_WITH_UNACCENT"
)

create_source_groups(nunicode)
