#include "FileSortMenuActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "CrossPointSettings.h"
#include "components/UITheme.h"
#include "fontIds.h"

// Sort option index → (mode, direction)
static constexpr uint8_t SORT_MODES[FileSortMenuActivity::ITEM_COUNT] = {
    CrossPointSettings::SORT_NAME, CrossPointSettings::SORT_NAME, CrossPointSettings::SORT_DATE,
    CrossPointSettings::SORT_DATE, CrossPointSettings::SORT_SIZE, CrossPointSettings::SORT_SIZE,
};
static constexpr uint8_t SORT_DIRS[FileSortMenuActivity::ITEM_COUNT] = {
    CrossPointSettings::SORT_ASC,  CrossPointSettings::SORT_DESC, CrossPointSettings::SORT_ASC,
    CrossPointSettings::SORT_DESC, CrossPointSettings::SORT_ASC,  CrossPointSettings::SORT_DESC,
};
static const StrId SORT_LABELS[FileSortMenuActivity::ITEM_COUNT] = {
    StrId::STR_SORT_NAME_ASC,  StrId::STR_SORT_NAME_DESC,  StrId::STR_SORT_DATE_ASC,
    StrId::STR_SORT_DATE_DESC, StrId::STR_SORT_SIZE_ASC,   StrId::STR_SORT_SIZE_DESC,
};

FileSortMenuActivity::FileSortMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("FileSortMenu", renderer, mappedInput) {}

void FileSortMenuActivity::onEnter() {
  Activity::onEnter();

  // Pre-select the currently active sort option
  const uint8_t mode = SETTINGS.fileSortMode;
  const uint8_t dir = SETTINGS.fileSortDirection;
  selectorIndex = 0;
  for (int i = 0; i < ITEM_COUNT; i++) {
    if (SORT_MODES[i] == mode && SORT_DIRS[i] == dir) {
      selectorIndex = static_cast<size_t>(i);
      break;
    }
  }

  requestUpdate(true);
}

void FileSortMenuActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    SETTINGS.fileSortMode = SORT_MODES[selectorIndex];
    SETTINGS.fileSortDirection = SORT_DIRS[selectorIndex];
    SETTINGS.saveToFile();

    MenuResult result;
    result.action = 0;
    setResult(ActivityResult{result});
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult res;
    res.isCancelled = true;
    setResult(std::move(res));
    finish();
    return;
  }

  buttonNavigator.onNextRelease([this] {
    selectorIndex = ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), ITEM_COUNT);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this] {
    selectorIndex = ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), ITEM_COUNT);
    requestUpdate();
  });
}

void FileSortMenuActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_SORT_BY));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  GUI.drawList(renderer, Rect{0, contentTop, pageWidth, contentHeight}, ITEM_COUNT,
               static_cast<int>(selectorIndex),
               [](int index) { return std::string(I18N.get(SORT_LABELS[index])); });

  const auto btnLabels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, btnLabels.btn1, btnLabels.btn2, btnLabels.btn3, btnLabels.btn4);

  renderer.displayBuffer();
}
