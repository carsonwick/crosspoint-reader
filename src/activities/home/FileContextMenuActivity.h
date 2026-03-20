#pragma once

#include <string>

#include "../Activity.h"
#include "util/ButtonNavigator.h"

class FileContextMenuActivity final : public Activity {
 public:
  FileContextMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string filename);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  static constexpr int ITEM_COUNT = 3;
  std::string filename;
  size_t selectorIndex = 0;
  ButtonNavigator buttonNavigator;
};
