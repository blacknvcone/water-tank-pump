#ifndef WATER_LEVEL_H
#define WATER_LEVEL_H

#include <Arduino.h>
#include "../config/settings.h"

class WaterLevelSensor {
public:
  WaterLevelSensor();
  
  void begin();
  void update();
  
  // Get sensor states
  bool isLowWaterDetected() const;
  bool isHighWaterDetected() const;
  
  // Check if sensor states have changed
  bool hasLowSensorChanged() const;
  bool hasHighSensorChanged() const;
  
  // Reset change flags after handling
  void resetChangeFlags();

private:
  bool _lowSensorState;
  bool _highSensorState;
  bool _lastLowSensorState;
  bool _lastHighSensorState;
  
  // Helper function to read sensors with debounce
  bool readSensor(int pin);
};

extern WaterLevelSensor waterLevel;

#endif // WATER_LEVEL_H