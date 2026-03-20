#include "FileContextMenuActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "components/UITheme.h"
#include "fontIds.h"

FileContextMenuActivity::FileContextMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                 std::string filename)
    : Activity("FileContextMenu", renderer, mappedInput), filename(std::move(filename)) {}

void FileContextMenuActivity::onEnter() {
  Activity::onEnter();
  selectorIndex = 0;
  requestUpdate(true);
}

void FileContextMenuActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    MenuResult result;
    result.action = static_cast<int>(selectorIndex);
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

void FileContextMenuActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, filename.c_str());

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  static const StrId labels[ITEM_COUNT] = {StrId::STR_SORT_BY, StrId::STR_DELETE, StrId::STR_FILE_INFO};

  GUI.drawList(renderer, Rect{0, contentTop, pageWidth, contentHeight}, ITEM_COUNT,
               static_cast<int>(selectorIndex),
               [](int index) { return std::string(I18N.get(labels[index])); });

  const auto btnLabels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, btnLabels.btn1, btnLabels.btn2, btnLabels.btn3, btnLabels.btn4);

  renderer.displayBuffer();
}
