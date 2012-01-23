INCLUDEPATH += $$PWD
DEPENDPATH += $$PWD

TRANSLATIONS += lang/quiterss_en.ts lang/quiterss_de.ts lang/quiterss_ru.ts

isEmpty(QMAKE_LRELEASE) {
  Q_WS_WIN:QMAKE_LRELEASE = $$[QT_INSTALL_BINS]\lrelease.exe
  else:QMAKE_LRELEASE = $$[QT_INSTALL_BINS]/lrelease
}

updateqm.input = TRANSLATIONS
updateqm.output = $${BUILD_DIR}/lang/${QMAKE_FILE_BASE}.qm
updateqm.commands = $$QMAKE_LRELEASE ${QMAKE_FILE_IN} -qm $${BUILD_DIR}/lang/${QMAKE_FILE_BASE}.qm
updateqm.CONFIG += no_link target_predeps
QMAKE_EXTRA_COMPILERS += updateqm
