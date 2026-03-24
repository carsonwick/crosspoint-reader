#pragma once

#include <HalGPIO.h>

class MappedInputManager {
 public:
  enum class Button { Back, Confirm, Left, Right, Up, Down, Power, PageBack, PageForward };

  struct Labels {
    const char* btn1;
    const char* btn2;
    const char* btn3;
    const char* btn4;
  };

  explicit MappedInputManager(HalGPIO& gpio) : gpio(gpio) {}

  void update() const;
  bool wasPressed(Button button) const;
  bool wasReleased(Button button) const;
  bool isPressed(Button button) const;
  bool wasAnyPressed() const;
  bool wasAnyReleased() const;
  unsigned long getHeldTime() const;
  Labels mapLabels(const char* back, const char* confirm, const char* previous, const char* next) const;
  // Returns the raw front button index that was pressed this frame (or -1 if none).
  int getPressedFrontButton() const;

  // Returns true exactly once when button has been held for thresholdMs.
  // Subsequent calls return false until the button is released and pressed again.
  bool wasLongPressed(Button button, unsigned long thresholdMs) const;

  // Returns true if wasLongPressed already fired for button (remains true until next update after release).
  // Use this to suppress the release event that follows a long press.
  bool isLongPressHandled(Button button) const;

 private:
  HalGPIO& gpio;

  bool mapButton(Button button, bool (HalGPIO::*fn)(uint8_t) const) const;

  mutable bool longPressFired_ = false;
  mutable bool pendingLongPressReset_ = false;
  mutable uint8_t longPressButton_ = 0;
};
