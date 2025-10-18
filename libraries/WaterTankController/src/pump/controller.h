#ifndef PUMP_CONTROLLER_H
#define PUMP_CONTROLLER_H

#include <Arduino.h>
#include <time.h>
#include "../config/settings.h"
#include "../sensors/water_level.h"

class PumpController {
public:
  PumpController();

  void begin();
  void loop();

  // Control methods
  void setOverrideMode(bool enabled, bool state);
  bool isOverrideMode() const;
  bool getPumpState() const;

  // State change detection
  bool hasPumpStateChanged();

  // Timing information
  unsigned long getLastOnTime() const;
  unsigned long getLastOffTime() const;
  time_t getLastOnEpoch() const;
  time_t getLastOffEpoch() const;

  // Manual control for updating timestamps
  void updateTimestamps(time_t currentTime);

private:
  bool _overrideMode;
  bool _overrideState;
  bool _pumpState;
  bool _lastPumpState;

  // Timestamps
  unsigned long _pumpLastOnAt;   // milliseconds since boot
  unsigned long _pumpLastOffAt;  // milliseconds since boot
  time_t _pumpLastOnEpoch;       // seconds since epoch (UTC)
  time_t _pumpLastOffEpoch;      // seconds since epoch (UTC)

  void setPumpState(bool state, time_t currentTime);
  void handleAutomaticControl(time_t currentTime);
};

extern PumpController pumpController;

#endif // PUMP_CONTROLLER_H
