#pragma once

#include <cstdint>
#include <string>

#include "../Activity.h"

class FileInfoActivity final : public Activity {
 public:
  FileInfoActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string filename, std::string basepath,
                   uint32_t fileSize);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  std::string filename;
  std::string basepath;
  uint32_t fileSize;
};
