#include "controller.h"

PumpController pumpController;

PumpController::PumpController() :
  _overrideMode(false),
  _overrideState(false),
  _pumpState(false),
  _lastPumpState(false),
  _pumpLastOnAt(0),
  _pumpLastOffAt(0),
  _pumpLastOnEpoch(0),
  _pumpLastOffEpoch(0) {
}

void PumpController::begin() {
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW); // Initially OFF

  Serial.println("Pump controller initialized");
}

void PumpController::loop() {
  time_t currentTime = time(nullptr);

  if (_overrideMode) {
    // Manual override mode - use override state
    if (_pumpState != _overrideState) {
      setPumpState(_overrideState, currentTime);
    }
  } else {
    // Automatic mode based on water level sensors
    handleAutomaticControl(currentTime);
  }

  _lastPumpState = _pumpState;
}

void PumpController::setOverrideMode(bool enabled, bool state) {
  _overrideMode = enabled;
  _overrideState = state;

  Serial.print("Override mode ");
  Serial.print(enabled ? "ENABLED" : "DISABLED");
  if (enabled) {
    Serial.print(" - State: ");
    Serial.println(state ? "ON" : "OFF");
  } else {
    Serial.println();
  }
}

bool PumpController::isOverrideMode() const {
  return _overrideMode;
}

bool PumpController::getPumpState() const {
  return _pumpState;
}

bool PumpController::hasPumpStateChanged() {
  return _pumpState != _lastPumpState;
}

unsigned long PumpController::getLastOnTime() const {
  return _pumpLastOnAt;
}

unsigned long PumpController::getLastOffTime() const {
  return _pumpLastOffAt;
}

time_t PumpController::getLastOnEpoch() const {
  return _pumpLastOnEpoch;
}

time_t PumpController::getLastOffEpoch() const {
  return _pumpLastOffEpoch;
}

void PumpController::updateTimestamps(time_t currentTime) {
  // Update epoch timestamps if time is synced and we have boot timestamps
  if (currentTime > 1000000000) {
    unsigned long currentMillis = millis();

    if (_pumpLastOnAt > 0) {
      unsigned long elapsedSinceOn = currentMillis - _pumpLastOnAt;
      _pumpLastOnEpoch = currentTime - (elapsedSinceOn / 1000);
    }

    if (_pumpLastOffAt > 0) {
      unsigned long elapsedSinceOff = currentMillis - _pumpLastOffAt;
      _pumpLastOffEpoch = currentTime - (elapsedSinceOff / 1000);
    }
  }
}

void PumpController::setPumpState(bool state, time_t currentTime) {
  if (_pumpState != state) {
    _pumpState = state;
    digitalWrite(RELAY_PIN, state ? HIGH : LOW);

    Serial.print("Pump ");
    Serial.println(state ? "ON" : "OFF");

    // Update timestamps
    if (state) {
      _pumpLastOnAt = millis();
      if (currentTime > 1000000000) {
        _pumpLastOnEpoch = currentTime;
      }
    } else {
      _pumpLastOffAt = millis();
      if (currentTime > 1000000000) {
        _pumpLastOffEpoch = currentTime;
      }
    }
  }
}

void PumpController::handleAutomaticControl(time_t currentTime) {
  // Update water level sensor readings
  waterLevel.update();

  bool lowWater = waterLevel.isLowWaterDetected();
  bool highWater = waterLevel.isHighWaterDetected();

  // Pump control logic:
  // - Turn ON when low sensor is triggered (water level is low)
  // - Turn OFF when high sensor is triggered (water level is high)
  // - Maintain current state if both or neither sensors are triggered

  if (lowWater && !highWater) {
    // Water is low, turn pump ON
    setPumpState(true, currentTime);
  } else if (highWater) {
    // Water is high, turn pump OFF
    setPumpState(false, currentTime);
  }
  // If neither sensor is triggered, maintain current state (hysteresis)
}
