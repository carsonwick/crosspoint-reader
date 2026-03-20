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
  // Overlay — no clearScreen(); draw on top of the file list
  const auto pw = renderer.getScreenWidth();
  const auto ph = renderer.getScreenHeight();
  const auto& m = UITheme::getInstance().getMetrics();

  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int contentH = 2 * lineHeight + m.verticalSpacing;
  const int overlayW = pw - 2 * m.contentSidePadding;
  const int overlayX = m.contentSidePadding;
  const int overlayH = m.headerHeight + m.verticalSpacing + contentH + m.verticalSpacing;
  const int overlayY = (ph - m.buttonHintsHeight - overlayH) / 2;

  renderer.fillRect(overlayX - 2, overlayY - 2, overlayW + 4, overlayH + 4, true);
  renderer.fillRect(overlayX, overlayY, overlayW, overlayH, false);

  // Header: black bar with centred filename — no battery indicator
  const int innerW = overlayW - 2 * m.contentSidePadding;
  const std::string safeTitle = renderer.truncatedText(UI_12_FONT_ID, filename.c_str(), innerW, EpdFontFamily::BOLD);
  renderer.fillRect(overlayX, overlayY, overlayW, m.headerHeight, true);
  const int titleW = renderer.getTextWidth(UI_12_FONT_ID, safeTitle.c_str(), EpdFontFamily::BOLD);
  const int titleX = overlayX + (overlayW - titleW) / 2;
  renderer.drawText(UI_12_FONT_ID, titleX, overlayY + 5, safeTitle.c_str(), false, EpdFontFamily::BOLD);

  const int contentTop = overlayY + m.headerHeight + m.verticalSpacing;

  // Row 1: Path
  char pathLine[256];
  snprintf(pathLine, sizeof(pathLine), "%s: %s", tr(STR_PATH), basepath.c_str());
  const std::string safePath = renderer.truncatedText(UI_10_FONT_ID, pathLine, innerW);
  renderer.drawText(UI_10_FONT_ID, overlayX + m.contentSidePadding, contentTop, safePath.c_str(), true);

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
  renderer.drawText(UI_10_FONT_ID, overlayX + m.contentSidePadding, contentTop + lineHeight + m.verticalSpacing,
                    sizeLine, true);

  const auto btnLabels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, btnLabels.btn1, btnLabels.btn2, btnLabels.btn3, btnLabels.btn4);

  renderer.displayBuffer();
}
