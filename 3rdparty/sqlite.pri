#DEFINES += SQLITEDRIVER_DEBUG
#DEFINES += SQLITEDRIVER_EXPORT

INCLUDEPATH += $$PWD \
               $$PWD/sqlitex

os2|win32|mac {
  CONFIG(release, debug|release):DEFINES *= NDEBUG
  DEFINES += SQLITE_OMIT_LOAD_EXTENSION SQLITE_OMIT_COMPLETE

  isEqual(QT_MAJOR_VERSION, 5):isEqual(QT_MINOR_VERSION, 4) {
    HEADERS +=      $$PWD/sqlite_qt54x/sqlite3.h
    SOURCES +=      $$PWD/sqlite_qt54x/sqlite3.c
  } else {
    isEqual(QT_MAJOR_VERSION, 4):isEqual(QT_MINOR_VERSION, 7) {
      HEADERS +=      $$PWD/sqlite_qt47x/sqlite3.h
      SOURCES +=      $$PWD/sqlite_qt47x/sqlite3.c
    } else {
      HEADERS +=      $$PWD/sqlite_qt48x/sqlite3.h
      SOURCES +=      $$PWD/sqlite_qt48x/sqlite3.c
    }
  }
} else {
  CONFIG += link_pkgconfig
  PKGCONFIG += sqlite3
}

HEADERS += $$PWD/sqlitex/sqlcachedresult.h \
           $$PWD/sqlitex/sqlitedriver.h \
           $$PWD/sqlitex/sqliteextension.h

SOURCES += $$PWD/sqlitex/sqlcachedresult.cpp \
           $$PWD/sqlitex/sqlitedriver.cpp \
           $$PWD/sqlitex/sqliteextension.cpp
