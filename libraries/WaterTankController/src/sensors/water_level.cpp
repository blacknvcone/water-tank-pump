#include "water_level.h"

WaterLevelSensor waterLevel;

WaterLevelSensor::WaterLevelSensor() : 
  _lowSensorState(false),
  _highSensorState(false),
  _lastLowSensorState(false),
  _lastHighSensorState(false) {
}

void WaterLevelSensor::begin() {
  pinMode(LOW_SENSOR_PIN, INPUT);
  pinMode(HIGH_SENSOR_PIN, INPUT);
  
  // Initial read
  update();
  
  // Initialize last states to match current states
  _lastLowSensorState = _lowSensorState;
  _lastHighSensorState = _highSensorState;
}

void WaterLevelSensor::update() {
  // Store previous states
  _lastLowSensorState = _lowSensorState;
  _lastHighSensorState = _highSensorState;
  
  // Read current states
  _lowSensorState = readSensor(LOW_SENSOR_PIN);
  _highSensorState = readSensor(HIGH_SENSOR_PIN);
}

bool WaterLevelSensor::isLowWaterDetected() const {
  return _lowSensorState;
}

bool WaterLevelSensor::isHighWaterDetected() const {
  return _highSensorState;
}

bool WaterLevelSensor::hasLowSensorChanged() const {
  return _lowSensorState != _lastLowSensorState;
}

bool WaterLevelSensor::hasHighSensorChanged() const {
  return _highSensorState != _lastHighSensorState;
}

void WaterLevelSensor::resetChangeFlags() {
  _lastLowSensorState = _lowSensorState;
  _lastHighSensorState = _highSensorState;
}

bool WaterLevelSensor::readSensor(int pin) {
  // Read multiple times for debounce
  int activeCount = 0;
  for (int i = 0; i < 5; i++) {
    if (digitalRead(pin) == HIGH) {
      activeCount++;
    }
    delay(10);
  }
  return activeCount >= 3; // Majority vote
}