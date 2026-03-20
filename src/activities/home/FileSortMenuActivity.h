#pragma once

#include "../Activity.h"
#include "util/ButtonNavigator.h"

class FileSortMenuActivity final : public Activity {
 public:
  static constexpr int ITEM_COUNT = 6;

  FileSortMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  size_t selectorIndex = 0;
  ButtonNavigator buttonNavigator;
};
