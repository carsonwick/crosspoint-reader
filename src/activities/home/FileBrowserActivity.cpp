#include "FileBrowserActivity.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "../util/ConfirmationActivity.h"
#include "FileContextMenuActivity.h"
#include "FileInfoActivity.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr unsigned long GO_HOME_MS = 1000;

bool isSupportedFile(std::string_view name) {
  return FsHelpers::hasEpubExtension(name) || FsHelpers::hasXtcExtension(name) || FsHelpers::hasTxtExtension(name) ||
         FsHelpers::hasMarkdownExtension(name) || FsHelpers::hasBmpExtension(name);
}
}  // namespace

// Natural case-insensitive string compare: returns negative/zero/positive
static int naturalCompare(const std::string& str1, const std::string& str2) {
  const char* s1 = str1.c_str();
  const char* s2 = str2.c_str();

  while (*s1 && *s2) {
    if (isdigit(static_cast<unsigned char>(*s1)) && isdigit(static_cast<unsigned char>(*s2))) {
      while (*s1 == '0') s1++;
      while (*s2 == '0') s2++;

      int len1 = 0, len2 = 0;
      while (isdigit(static_cast<unsigned char>(s1[len1]))) len1++;
      while (isdigit(static_cast<unsigned char>(s2[len2]))) len2++;

      if (len1 != len2) return len1 < len2 ? -1 : 1;

      for (int i = 0; i < len1; i++) {
        if (s1[i] != s2[i]) return s1[i] < s2[i] ? -1 : 1;
      }

      s1 += len1;
      s2 += len2;
    } else {
      char c1 = static_cast<char>(tolower(static_cast<unsigned char>(*s1)));
      char c2 = static_cast<char>(tolower(static_cast<unsigned char>(*s2)));
      if (c1 != c2) return c1 < c2 ? -1 : 1;
      s1++;
      s2++;
    }
  }

  if (*s1 == '\0' && *s2 != '\0') return -1;
  if (*s1 != '\0' && *s2 == '\0') return 1;
  return 0;
}

void FileBrowserActivity::sortFileList(std::vector<FileEntry>& entries) {
  const auto mode = SETTINGS.fileSortMode;
  const bool descending = SETTINGS.fileSortDirection == CrossPointSettings::SORT_DESC;

  std::sort(begin(entries), end(entries), [mode, descending](const FileEntry& a, const FileEntry& b) {
    const bool aDir = a.name.back() == '/';
    const bool bDir = b.name.back() == '/';
    if (aDir != bDir) return aDir;

    int cmp = 0;
    switch (mode) {
      case CrossPointSettings::SORT_DATE:
        cmp = (a.dateTime != b.dateTime) ? (a.dateTime < b.dateTime ? -1 : 1) : naturalCompare(a.name, b.name);
        break;
      case CrossPointSettings::SORT_SIZE:
        cmp = (a.size != b.size) ? (a.size < b.size ? -1 : 1) : naturalCompare(a.name, b.name);
        break;
      default:  // SORT_NAME
        cmp = naturalCompare(a.name, b.name);
        break;
    }
    return descending ? (cmp > 0) : (cmp < 0);
  });
}

void FileBrowserActivity::loadFiles() {
  files.clear();
  files.reserve(64);

  auto root = Storage.open(basepath.c_str());
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    return;
  }

  root.rewindDirectory();

  uint32_t idx = 0;
  char name[500];
  for (auto file = root.openNextFile(); file; file = root.openNextFile()) {
    file.getName(name, sizeof(name));

    if (strcmp(name, "System Volume Information") == 0) {
      file.close();
      continue;
    }

    if (!SETTINGS.showHiddenFiles && name[0] == '.') {
      file.close();
      continue;
    }

    uint16_t mdate = 0, mtime = 0, cdate = 0, ctime = 0;
    file.getModifyDateTime(&mdate, &mtime);
    file.getCreateDateTime(&cdate, &ctime);
    const uint32_t modTs = (static_cast<uint32_t>(mdate) << 16) | mtime;
    const uint32_t crtTs = (static_cast<uint32_t>(cdate) << 16) | ctime;
    const uint32_t dateTime = modTs > crtTs ? modTs : crtTs;

    if (file.isDirectory()) {
      files.push_back({std::string(name) + "/", 0, dateTime});
    } else if (isSupportedFile(name)) {
      files.push_back({std::string(name), static_cast<uint32_t>(file.fileSize()), dateTime});
    }

    file.close();
  }
  root.close();
  sortFileList(files);
}

void FileBrowserActivity::onEnter() {
  Activity::onEnter();

  loadFiles();
  selectorIndex = 0;

  requestUpdate();
}

void FileBrowserActivity::onExit() {
  Activity::onExit();
  files.clear();
}

void FileBrowserActivity::clearFileMetadata(const std::string& fullPath) {
  if (FsHelpers::hasEpubExtension(fullPath)) {
    Epub(fullPath, "/.crosspoint").clearCache();
    LOG_DBG("FileBrowser", "Cleared metadata cache for: %s", fullPath.c_str());
  }
}

void FileBrowserActivity::showContextMenu(std::string entryName, std::string cleanBasePath, uint32_t entrySize,
                                          bool isDir) {
  const std::string fullPath = cleanBasePath + entryName;

  startActivityForResult(
      std::make_unique<FileContextMenuActivity>(renderer, mappedInput, entryName, isDir),
      [this, entryName, cleanBasePath = std::move(cleanBasePath), entrySize, isDir,
       fullPath](const ActivityResult& res) {
        // Always reload — sort settings may have changed inside the context menu
        loadFiles();
        if (selectorIndex >= files.size()) {
          selectorIndex = files.empty() ? 0 : files.size() - 1;
        }

        if (!res.isCancelled) {
          const auto* menuRes = std::get_if<MenuResult>(&res.data);
          if (menuRes) {
            switch (menuRes->action) {
              case 1:  // Delete — return to context menu if cancelled
                doDelete(fullPath, isDir, [this, entryName, cleanBasePath, entrySize, isDir] {
                  showContextMenu(entryName, cleanBasePath, entrySize, isDir);
                });
                return;
              case 2:  // File info — always return to context menu after closing
                startActivityForResult(
                    std::make_unique<FileInfoActivity>(renderer, mappedInput, entryName, cleanBasePath, entrySize),
                    [this, entryName, cleanBasePath, entrySize, isDir](const ActivityResult&) {
                      showContextMenu(entryName, cleanBasePath, entrySize, isDir);
                    });
                return;
              default:
                break;
            }
          }
        }

        requestUpdate(true);
      });
}

void FileBrowserActivity::doDelete(const std::string& fullPath, bool isDirectory, std::function<void()> onCancel) {
  auto handler = [this, fullPath, isDirectory, onCancel = std::move(onCancel)](const ActivityResult& res) {
    if (!res.isCancelled) {
      LOG_DBG("FileBrowser", "Attempting to delete: %s", fullPath.c_str());

      bool ok;
      if (isDirectory) {
        // removeDir requires path without trailing slash
        std::string dirPath = fullPath;
        if (!dirPath.empty() && dirPath.back() == '/') dirPath.pop_back();
        ok = Storage.removeDir(dirPath.c_str());
        // Note: orphaned .crosspoint cache entries for books inside the folder
        // are harmless and will be ignored on next access.
      } else {
        clearFileMetadata(fullPath);
        ok = Storage.remove(fullPath.c_str());
      }

      if (ok) {
        LOG_DBG("FileBrowser", "Deleted successfully");
        loadFiles();
        if (files.empty()) {
          selectorIndex = 0;
        } else if (selectorIndex >= files.size()) {
          selectorIndex = files.size() - 1;
        }
        requestUpdate(true);
      } else {
        LOG_ERR("FileBrowser", "Failed to delete: %s", fullPath.c_str());
      }
    } else {
      LOG_DBG("FileBrowser", "Delete cancelled by user");
      if (onCancel) onCancel();
    }
  };

  const std::string heading =
      isDirectory ? (tr(STR_DELETE_FOLDER) + std::string("? ")) : (tr(STR_DELETE) + std::string("? "));
  startActivityForResult(std::make_unique<ConfirmationActivity>(renderer, mappedInput, heading, fullPath), handler);
}

void FileBrowserActivity::loop() {
  // Long press BACK (1s+) goes to root folder
  if (mappedInput.isPressed(MappedInputManager::Button::Back) && mappedInput.getHeldTime() >= GO_HOME_MS &&
      basepath != "/") {
    basepath = "/";
    loadFiles();
    selectorIndex = 0;
    return;
  }

  const int pathReserved = renderer.getLineHeight(SMALL_FONT_ID) + UITheme::getInstance().getMetrics().verticalSpacing;
  const int pageItems = UITheme::getNumberOfItemsPerPage(renderer, true, false, true, false, pathReserved);

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (files.empty()) return;

    const FileEntry& entry = files[selectorIndex];
    const bool isDirectory = (entry.name.back() == '/');

    if (mappedInput.getHeldTime() >= GO_HOME_MS) {
      // --- LONG PRESS: show context menu (files and directories) ---
      std::string cleanBasePath = basepath;
      if (cleanBasePath.back() != '/') cleanBasePath += "/";
      showContextMenu(entry.name, std::move(cleanBasePath), entry.size, isDirectory);
      return;
    } else {
      // --- SHORT PRESS: open/navigate ---
      if (basepath.back() != '/') basepath += "/";

      if (isDirectory) {
        basepath += entry.name.substr(0, entry.name.length() - 1);
        loadFiles();
        selectorIndex = 0;
        requestUpdate();
      } else {
        onSelectBook(basepath + entry.name);
      }
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (mappedInput.getHeldTime() < GO_HOME_MS) {
      if (basepath != "/") {
        const std::string oldPath = basepath;

        basepath.replace(basepath.find_last_of('/'), std::string::npos, "");
        if (basepath.empty()) basepath = "/";
        loadFiles();

        const auto pos = oldPath.find_last_of('/');
        const std::string dirName = oldPath.substr(pos + 1) + "/";
        selectorIndex = findEntry(dirName);

        requestUpdate();
      } else {
        onGoHome();
      }
    }
  }

  int listSize = static_cast<int>(files.size());
  buttonNavigator.onNextRelease([this, listSize] {
    selectorIndex = ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), listSize);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this, listSize] {
    selectorIndex = ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), listSize);
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this, listSize, pageItems] {
    selectorIndex = ButtonNavigator::nextPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this, listSize, pageItems] {
    selectorIndex = ButtonNavigator::previousPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
    requestUpdate();
  });
}

static std::string getFileName(const std::string& filename) {
  if (filename.back() == '/') {
    return filename.substr(0, filename.length() - 1);
  }
  const auto pos = filename.rfind('.');
  return filename.substr(0, pos);
}

void FileBrowserActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  std::string folderName = (basepath == "/") ? tr(STR_SD_CARD) : basepath.substr(basepath.rfind('/') + 1);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, folderName.c_str());

  const int pathLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int pathReserved = pathLineHeight + metrics.verticalSpacing;
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing - pathReserved;
  if (files.empty()) {
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + 20, tr(STR_NO_FILES_FOUND));
  } else {
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, files.size(), selectorIndex,
        [this](int index) { return getFileName(files[index].name); }, nullptr,
        [this](int index) { return UITheme::getFileIcon(files[index].name); });
  }

  // Full path display
  {
    const int pathY = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing - pathLineHeight;
    const int separatorY = pathY - metrics.verticalSpacing / 2;
    renderer.drawLine(0, separatorY, pageWidth - 1, separatorY, 3, true);
    const int pathMaxWidth = pageWidth - metrics.contentSidePadding * 2;
    // Left-truncate so the deepest directory is always visible
    const char* pathStr = basepath.c_str();
    const char* display = pathStr;
    char leftTruncBuf[256];
    if (renderer.getTextWidth(SMALL_FONT_ID, pathStr) > pathMaxWidth) {
      const char ellipsis[] = "\xe2\x80\xa6";  // UTF-8 ellipsis (…)
      const int ellipsisWidth = renderer.getTextWidth(SMALL_FONT_ID, ellipsis);
      const int available = pathMaxWidth - ellipsisWidth;
      // Walk forward from the start until the suffix fits, skipping UTF-8 continuation bytes
      const char* p = pathStr;
      while (*p) {
        if (renderer.getTextWidth(SMALL_FONT_ID, p) <= available) break;
        ++p;
        while (*p && (static_cast<unsigned char>(*p) & 0xC0) == 0x80) ++p;
      }
      snprintf(leftTruncBuf, sizeof(leftTruncBuf), "%s%s", ellipsis, p);
      display = leftTruncBuf;
    }
    renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, pathY, display);
  }

  const auto labels =
      mappedInput.mapLabels(basepath == "/" ? tr(STR_HOME) : tr(STR_BACK), files.empty() ? "" : tr(STR_OPEN),
                            files.empty() ? "" : tr(STR_DIR_UP), files.empty() ? "" : tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

size_t FileBrowserActivity::findEntry(const std::string& name) const {
  for (size_t i = 0; i < files.size(); i++)
    if (files[i].name == name) return i;
  return 0;
}
