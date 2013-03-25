INCLUDEPATH += $$PWD
DEPENDPATH += $$PWD

TRANSLATIONS += lang/quiterss_en.ts lang/quiterss_de.ts lang/quiterss_ru.ts \
                lang/quiterss_es.ts lang/quiterss_fr.ts lang/quiterss_hu.ts \
                lang/quiterss_sv.ts lang/quiterss_sr.ts lang/quiterss_nl.ts \
                lang/quiterss_fa.ts lang/quiterss_it.ts lang/quiterss_zh_cn.ts \
                lang/quiterss_uk.ts lang/quiterss_cs_cz.ts lang/quiterss_pl.ts \
                lang/quiterss_ja.ts lang/quiterss_ko.ts lang/quiterss_pt_br.ts \
                lang/quiterss_lt.ts

isEmpty(QMAKE_LRELEASE) {
  Q_WS_WIN:QMAKE_LRELEASE = $$[QT_INSTALL_BINS]\lrelease.exe
  else:QMAKE_LRELEASE = $$[QT_INSTALL_BINS]/lrelease
}

updateqm.input = TRANSLATIONS
updateqm.output = $$DESTDIR/lang/${QMAKE_FILE_BASE}.qm
updateqm.commands = $$QMAKE_LRELEASE ${QMAKE_FILE_IN} -qm $$DESTDIR/lang/${QMAKE_FILE_BASE}.qm
updateqm.CONFIG += no_link target_predeps
QMAKE_EXTRA_COMPILERS += updateqm
