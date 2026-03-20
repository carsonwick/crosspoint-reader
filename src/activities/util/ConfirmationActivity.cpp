#include "ConfirmationActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "../../components/UITheme.h"
#include "HalDisplay.h"

ConfirmationActivity::ConfirmationActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                           const std::string& heading, const std::string& body, bool overlay)
    : Activity("Confirmation", renderer, mappedInput), heading(heading), body(body), overlay(overlay) {}

void ConfirmationActivity::onEnter() {
  Activity::onEnter();

  lineHeight = renderer.getLineHeight(fontId);
  const int maxWidth = renderer.getScreenWidth() - (margin * 2);

  if (!heading.empty()) {
    safeHeading = renderer.truncatedText(fontId, heading.c_str(), maxWidth, EpdFontFamily::BOLD);
  }
  if (!body.empty()) {
    safeBody = renderer.truncatedText(fontId, body.c_str(), maxWidth, EpdFontFamily::REGULAR);
  }

  int totalHeight = 0;
  if (!safeHeading.empty()) totalHeight += lineHeight;
  if (!safeBody.empty()) totalHeight += lineHeight;
  if (!safeHeading.empty() && !safeBody.empty()) totalHeight += spacing;

  startY = (renderer.getScreenHeight() - totalHeight) / 2;

  requestUpdate(true);
}

void ConfirmationActivity::render(RenderLock&& lock) {
  if (!overlay) {
    renderer.clearScreen();
  } else {
    const auto pw = renderer.getScreenWidth();
    const auto ph = renderer.getScreenHeight();
    const auto& m = UITheme::getInstance().getMetrics();

    const int totalTextH = (!safeHeading.empty() ? lineHeight : 0) +
                           (!safeHeading.empty() && !safeBody.empty() ? spacing : 0) +
                           (!safeBody.empty() ? lineHeight : 0);
    const int overlayW = pw - 2 * m.contentSidePadding;
    const int overlayX = m.contentSidePadding;
    const int overlayH = margin * 2 + totalTextH;
    const int overlayY = (ph - m.buttonHintsHeight - overlayH) / 2;

    renderer.fillRect(overlayX - 2, overlayY - 2, overlayW + 4, overlayH + 4, true);
    renderer.fillRect(overlayX, overlayY, overlayW, overlayH, false);

    startY = overlayY + margin;
  }

  int currentY = startY;
  // Draw Heading
  if (!safeHeading.empty()) {
    renderer.drawCenteredText(fontId, currentY, safeHeading.c_str(), true, EpdFontFamily::BOLD);
    currentY += lineHeight + spacing;
  }

  // Draw Body
  if (!safeBody.empty()) {
    renderer.drawCenteredText(fontId, currentY, safeBody.c_str(), true, EpdFontFamily::REGULAR);
  }

  const auto labels = mappedInput.mapLabels("", "", I18N.get(StrId::STR_CANCEL), I18N.get(StrId::STR_CONFIRM));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer(HalDisplay::RefreshMode::FAST_REFRESH);
}

void ConfirmationActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    ActivityResult res;
    res.isCancelled = false;
    setResult(std::move(res));
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    ActivityResult res;
    res.isCancelled = true;
    setResult(std::move(res));
    finish();
    return;
  }
}