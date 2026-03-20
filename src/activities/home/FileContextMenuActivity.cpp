#include "FileContextMenuActivity.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include "CrossPointSettings.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int MAIN_ITEM_COUNT = 3;
constexpr int MAIN_ITEM_COUNT_EPUB = 4;
constexpr int SORT_ITEM_COUNT = 6;

const StrId MAIN_LABELS[] = {StrId::STR_SORT_BY, StrId::STR_DELETE, StrId::STR_FILE_INFO};

const StrId SORT_LABELS[] = {
    StrId::STR_SORT_NAME_ASC,  StrId::STR_SORT_NAME_DESC, StrId::STR_SORT_DATE_ASC,
    StrId::STR_SORT_DATE_DESC, StrId::STR_SORT_SIZE_ASC,  StrId::STR_SORT_SIZE_DESC,
};

constexpr uint8_t SORT_MODES[] = {
    CrossPointSettings::SORT_NAME, CrossPointSettings::SORT_NAME, CrossPointSettings::SORT_DATE,
    CrossPointSettings::SORT_DATE, CrossPointSettings::SORT_SIZE, CrossPointSettings::SORT_SIZE,
};

constexpr uint8_t SORT_DIRS[] = {
    CrossPointSettings::SORT_ASC,  CrossPointSettings::SORT_DESC, CrossPointSettings::SORT_ASC,
    CrossPointSettings::SORT_DESC, CrossPointSettings::SORT_ASC,  CrossPointSettings::SORT_DESC,
};
}  // namespace

FileContextMenuActivity::FileContextMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                 std::string filename, bool isDirectory)
    : Activity("FileContextMenu", renderer, mappedInput), filename(std::move(filename)), isDirectory(isDirectory) {}

void FileContextMenuActivity::onEnter() {
  Activity::onEnter();
  state = State::MAIN;
  mainIndex = 0;

  // Pre-select the currently active sort option
  const uint8_t mode = SETTINGS.fileSortMode;
  const uint8_t dir = SETTINGS.fileSortDirection;
  sortIndex = 0;
  for (int i = 0; i < SORT_ITEM_COUNT; i++) {
    if (SORT_MODES[i] == mode && SORT_DIRS[i] == dir) {
      sortIndex = static_cast<size_t>(i);
      break;
    }
  }

  requestUpdate(true);
}

static int mainItemCount(bool isDirectory, const std::string& filename) {
  if (isDirectory) return 2;
  if (FsHelpers::hasEpubExtension(filename)) return MAIN_ITEM_COUNT_EPUB;
  return MAIN_ITEM_COUNT;
}

void FileContextMenuActivity::loop() {
  const int mainCount = mainItemCount(isDirectory, filename);
  const int itemCount = (state == State::MAIN) ? mainCount : SORT_ITEM_COUNT;

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (state == State::MAIN) {
      if (mainIndex == 0) {
        state = State::SORT;
        requestUpdate(true);
      } else {
        MenuResult result;
        result.action = static_cast<int>(mainIndex);
        setResult(ActivityResult{result});
        finish();
      }
    } else {
      SETTINGS.fileSortMode = SORT_MODES[sortIndex];
      SETTINGS.fileSortDirection = SORT_DIRS[sortIndex];
      SETTINGS.saveToFile();
      state = State::MAIN;
      requestUpdate(true);
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (state == State::SORT) {
      state = State::MAIN;
      requestUpdate(true);
    } else {
      ActivityResult res;
      res.isCancelled = true;
      setResult(std::move(res));
      finish();
    }
    return;
  }

  buttonNavigator.onNextRelease([this, itemCount] {
    if (state == State::MAIN) {
      mainIndex = static_cast<size_t>(ButtonNavigator::nextIndex(static_cast<int>(mainIndex), itemCount));
    } else {
      sortIndex = static_cast<size_t>(ButtonNavigator::nextIndex(static_cast<int>(sortIndex), itemCount));
    }
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this, itemCount] {
    if (state == State::MAIN) {
      mainIndex = static_cast<size_t>(ButtonNavigator::previousIndex(static_cast<int>(mainIndex), itemCount));
    } else {
      sortIndex = static_cast<size_t>(ButtonNavigator::previousIndex(static_cast<int>(sortIndex), itemCount));
    }
    requestUpdate();
  });
}

void FileContextMenuActivity::render(RenderLock&&) {
  // No clearScreen() — draw as overlay on top of the file list
  const auto pw = renderer.getScreenWidth();
  const auto ph = renderer.getScreenHeight();
  const auto& m = UITheme::getInstance().getMetrics();

  // Overlay is always sized for the larger state (6-item sort list) so the
  // box doesn't resize when switching between MAIN and SORT states.
  const int overlayW = pw - 2 * m.contentSidePadding;
  const int overlayX = m.contentSidePadding;
  const int listH = SORT_ITEM_COUNT * m.listRowHeight;
  const int overlayH = m.headerHeight + m.verticalSpacing + listH;
  const int overlayY = (ph - m.buttonHintsHeight - overlayH) / 2;

  // White box with 2px black border
  renderer.fillRect(overlayX - 2, overlayY - 2, overlayW + 4, overlayH + 4, true);
  renderer.fillRect(overlayX, overlayY, overlayW, overlayH, false);

  // Header: same style as regular header, battery suppressed
  const char* rawTitle = (state == State::MAIN) ? filename.c_str() : tr(STR_SORT_BY);
  GUI.drawHeader(renderer, Rect{overlayX, overlayY, overlayW, m.headerHeight}, rawTitle, nullptr, false);

  const int listTop = overlayY + m.headerHeight + m.verticalSpacing;
  const int selectedIndex = static_cast<int>(state == State::MAIN ? mainIndex : sortIndex);
  const int mainCount = mainItemCount(isDirectory, filename);
  const int itemCount = state == State::MAIN ? mainCount : SORT_ITEM_COUNT;

  if (state == State::MAIN) {
    GUI.drawList(renderer, Rect{overlayX, listTop, overlayW, listH}, itemCount, selectedIndex, [this](int i) {
      if (i == 1 && isDirectory) return std::string(I18N.get(StrId::STR_DELETE_FOLDER));
      if (i == 3) return std::string(I18N.get(StrId::STR_CLEAR_PROGRESS));
      return std::string(I18N.get(MAIN_LABELS[i]));
    });
  } else {
    GUI.drawList(renderer, Rect{overlayX, listTop, overlayW, listH}, itemCount, selectedIndex,
                 [](int i) { return std::string(I18N.get(SORT_LABELS[i])); });
  }

  const auto btnLabels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, btnLabels.btn1, btnLabels.btn2, btnLabels.btn3, btnLabels.btn4);

  renderer.displayBuffer();
}
