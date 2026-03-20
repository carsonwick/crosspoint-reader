#include "FileInfoActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <cstdio>

#include "components/UITheme.h"
#include "fontIds.h"

FileInfoActivity::FileInfoActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string filename,
                                   std::string basepath, uint32_t fileSize)
    : Activity("FileInfo", renderer, mappedInput),
      filename(std::move(filename)),
      basepath(std::move(basepath)),
      fileSize(fileSize) {}

void FileInfoActivity::onEnter() {
  Activity::onEnter();
  requestUpdate(true);
}

void FileInfoActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
      mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    ActivityResult res;
    res.isCancelled = true;
    setResult(std::move(res));
    finish();
  }
}

void FileInfoActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  // Truncate filename for header display
  const int maxWidth = pageWidth - metrics.contentSidePadding * 2;
  const std::string safeTitle = renderer.truncatedText(UI_10_FONT_ID, filename.c_str(), maxWidth);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, safeTitle.c_str());

  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;

  // Row 1: Path
  char pathLine[256];
  snprintf(pathLine, sizeof(pathLine), "%s: %s", tr(STR_PATH), basepath.c_str());
  const std::string safePath = renderer.truncatedText(UI_10_FONT_ID, pathLine, maxWidth);
  renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop, safePath.c_str(), true);

  // Row 2: File size
  char sizeBuf[32];
  if (fileSize >= 1024UL * 1024UL) {
    snprintf(sizeBuf, sizeof(sizeBuf), "%.1f MB", static_cast<float>(fileSize) / (1024.0f * 1024.0f));
  } else if (fileSize >= 1024UL) {
    snprintf(sizeBuf, sizeof(sizeBuf), "%.1f KB", static_cast<float>(fileSize) / 1024.0f);
  } else {
    snprintf(sizeBuf, sizeof(sizeBuf), "%lu B", static_cast<unsigned long>(fileSize));
  }

  char sizeLine[64];
  snprintf(sizeLine, sizeof(sizeLine), "%s: %s", tr(STR_FILE_SIZE), sizeBuf);
  renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + lineHeight + metrics.verticalSpacing,
                    sizeLine, true);

  const auto btnLabels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, btnLabels.btn1, btnLabels.btn2, btnLabels.btn3, btnLabels.btn4);

  renderer.displayBuffer();
}
