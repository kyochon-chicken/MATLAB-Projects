# Smart Cooling Road System (Arduino)

스마트시티 도로 냉각 시스템 프로젝트입니다.

온도, 습도, 미세먼지, 조도를 실시간으로 측정하여 환경을 분석하고,
설정된 알고리즘을 통해 가습기(미스트)를 자동으로 제어하는 Arduino 기반 시스템입니다.

---

## Features

- 🌡️ Temperature measurement (DHT11)
- 💧 Humidity measurement (DHT11)
- 🌫️ PM2.5 fine dust measurement (PM2008)
- ☀️ Day/Night detection using Light Sensor
- 💨 Automatic humidifier control
- 💡 RGB LED status indication
- 📟 16x2 I2C LCD real-time display
- 🔘 Power ON/OFF button
- 📊 Average filtering (3 samples)
- 📈 Environmental score calculation

---

## Hardware

- Arduino Uno
- DHT11 Temperature & Humidity Sensor
- PM2008 I2C Fine Dust Sensor
- I2C LCD 16x2
- Light Sensor
- RGB LED
- Push Button
- Humidifier Module
- Jumper Wires
- Breadboard

---

## Libraries

Install the following libraries before uploading.

- Wire
- LiquidCrystal_I2C
- DHT Sensor Library
- PM2008_I2C

---

## Pin Configuration

| Device | Pin |
|---------|-----|
| DHT11 | D2 |
| Humidifier Module | D9 |
| Red LED | D5 |
| Green LED | D6 |
| Blue LED | D10 |
| Power Button | D7 |
| Light Sensor | A2 |
| LCD (I2C) | SDA / SCL |
| PM2008 (I2C) | SDA / SCL |

---

## System Flow

1. System starts.
2. Collect sensor data.
3. Average 3 measurements.
4. Calculate environmental score.
5. Detect Day/Night using light sensor.
6. Control humidifier automatically.
7. Display information on LCD.
8. Indicate system status with RGB LED.

---

## Project Structure

```
Sensors
 ├── DHT11
 ├── PM2008
 └── Light Sensor

        │

 Arduino UNO
        │

 ├── LCD Display
 ├── RGB LED
 ├── Humidifier Module
 └── Power Button
```

---

## Environmental Score

The system evaluates the environment using:

```
Score = 0.56 × Humidity
      + 0.28 × PM2.5
      + 0.16 × Temperature
```

The calculated score is used for automatic humidifier control.

---

## Author

Developed for Smart City Cooling Road Project.
