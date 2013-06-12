INCLUDEPATH += $$PWD

CONFIG(release, debug|release):DEFINES *= NDEBUG
DEFINES += SQLITE_OMIT_LOAD_EXTENSION SQLITE_OMIT_COMPLETE

isEqual(QT_MAJOR_VERSION, 4):isEqual(QT_MINOR_VERSION, 7) {
  HEADERS +=      $$PWD/sqlite_qt47x/sqlite3.h
  SOURCES +=      $$PWD/sqlite_qt47x/sqlite3.c
} else {
  HEADERS +=      $$PWD/sqlite_qt48x/sqlite3.h
  SOURCES +=      $$PWD/sqlite_qt48x/sqlite3.c
}
