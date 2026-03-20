#pragma once

#include <string>

#include "../Activity.h"
#include "util/ButtonNavigator.h"

class FileContextMenuActivity final : public Activity {
 public:
  FileContextMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string filename,
                          bool isDirectory = false);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class State { MAIN, SORT, BATCH_ACTION_PICK };

  std::string filename;
  bool isDirectory;
  State state = State::MAIN;
  size_t mainIndex = 0;
  size_t sortIndex = 0;
  size_t batchIndex = 0;
  ButtonNavigator buttonNavigator;
};
