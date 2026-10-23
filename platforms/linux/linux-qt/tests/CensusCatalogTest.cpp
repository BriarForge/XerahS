// EDITOR-EFFECTS-001: EE-001..EE-003, EE-017.
#include "image-editor/effects/CensusCatalog.h"
#include <QSet>
#include <QTest>
#include <array>
#include <algorithm>

using namespace xerahs::effects;
class CensusCatalogTest : public QObject {
  Q_OBJECT
private slots:
  void censusMatchesSelectedCounts() {
    const auto &catalog = censusCatalog();
    QCOMPARE(catalog.size(), 232);
    QSet<QString> ids;
    std::array<int, 4> categories{};
    int previewable = 0, immediate = 0, parameters = 0;
    for (const auto &effect : catalog) {
      QVERIFY(!effect.id.isEmpty());
      QVERIFY(!ids.contains(effect.id));
      ids.insert(effect.id);
      ++categories.at(size_t(effect.category));
      if (effect.mode == Mode::Previewable) ++previewable;
      else if (effect.mode == Mode::Immediate) ++immediate;
      else QFAIL("Unknown execution mode");
      QSet<QString> parameterIds;
      for (const auto &p : effect.parameters) {
        QVERIFY(!p.id.isEmpty() && !p.label.isEmpty());
        QVERIFY(!parameterIds.contains(p.id));
        parameterIds.insert(p.id);
        ++parameters;
      }
    }
    QCOMPARE(categories[0], 32);
    QCOMPARE(categories[1], 16);
    QCOMPARE(categories[2], 149);
    QCOMPARE(categories[3], 35);
    QCOMPARE(previewable, 219);
    QCOMPARE(immediate, 13);
    QCOMPARE(parameters, 963);
  }
  void registeredEffectsMatchCensus() {
    const auto &registry = builtinEffects();
    QCOMPARE(registry.size(), 6);
    for (const auto *spec : registry.specs()) {
      const auto &catalog = censusCatalog();
      const auto entry = std::find_if(catalog.cbegin(), catalog.cend(),
                                      [&](const CensusEffect &e) { return e.id == spec->id; });
      QVERIFY2(entry != catalog.cend(), qPrintable(spec->id));
      QCOMPARE(spec->category, entry->category);
      QCOMPARE(spec->mode, entry->mode);
      QCOMPARE(spec->parameters.size(), entry->parameters.size());
      for (int i = 0; i < spec->parameters.size(); ++i) {
        QCOMPARE(spec->parameters[i].id, entry->parameters[i].id);
        QCOMPARE(spec->parameters[i].kind, entry->parameters[i].kind);
        QCOMPARE(spec->parameters[i].label, entry->parameters[i].label);
      }
      QVERIFY(!spec->changesSize && !spec->usesSeed);
      QVERIFY(spec->referencePath.startsWith("ShareX.ImageEditor/src/ShareX.ImageEditor/Core/ImageEffects/Adjustments/"));
      QVERIFY(resolveParameters(*spec, {}).ok());
    }
    for (const QString &id : {"alpha", "brightness", "contrast", "grayscale", "invert", "saturation"})
      QVERIFY(registry.find(id));
  }
};
QTEST_GUILESS_MAIN(CensusCatalogTest)
#include "CensusCatalogTest.moc"
