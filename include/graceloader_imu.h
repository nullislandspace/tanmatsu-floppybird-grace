// SPDX-License-Identifier: MIT
// Graceloader IMU API — orientation sensor access for dynamically-loaded apps
#pragma once

// The orientation API itself (enable/disable/get) is declared by badge-bsp.
// This header adds the Tanmatsu-specific axis documentation and the usage
// flow; include this header rather than bsp/orientation.h directly.
#include "bsp/orientation.h"

// ===========================================================================
// Orientation sensor (Bosch BMI270 — 3-axis accelerometer + 3-axis gyroscope)
// ===========================================================================
//
// Usage
// -----
// The BMI270 is brought up by the BSP as part of bsp_device_initialize() —
// the call your app already makes to initialize the display, input, etc.
// Graceloader itself initializes no hardware, so do NOT call any I2C or
// orientation init function yourself; that would collide with the BSP.
//
// After bsp_device_initialize() the sensor is initialized but idle. Enable
// the accelerometer and/or gyroscope once at startup, then read whenever:
//
//     bsp_device_initialize(&cfg);            // app already does this
//     bsp_orientation_enable_accelerometer(); // once, at startup
//     bsp_orientation_enable_gyroscope();     // once, at startup
//
//     bool g_ready, a_ready;
//     float gx, gy, gz, ax, ay, az;
//     bsp_orientation_get(&g_ready, &a_ready, &gx, &gy, &gz, &ax, &ay, &az);
//
// The accelerometer and gyroscope draw negligible power, so leaving both
// enabled for the whole app lifetime is fine. bsp_orientation_disable_*()
// is available if you want to turn a sensor off anyway.
//
// ---------------------------------------------------------------------------
// Axis orientation (how the BMI270 is mounted in the Tanmatsu)
// ---------------------------------------------------------------------------
//
// Readings are "specific force" / angular rate in the device's own reference
// frame. A motionless device measures gravity: each accelerometer axis reads
// between -9.81 and +9.81 m/s2 (1 g), and the axis pointing toward the ground
// reads about -9.7 m/s2.
//
//   Device pose                         Axis reading ~ -9.7 m/s2
//   ----------------------------------  ------------------------
//   Lying flat on a table, screen up    accel Z   (3rd value)
//   Held upright (normal viewing pose)  accel X   (1st value)
//   Left-hand side tilted toward floor  accel Y   (2nd value)
//
// From the poses above, the axes (right-handed) point as follows:
//
//   +X  along the device, from the TOP edge   toward the BOTTOM edge
//   +Y  across the device, from the RIGHT edge toward the LEFT edge
//   +Z  through the device, from the SCREEN   out through the BACK
//
//        held upright, screen facing you:
//
//              top edge
//             +---------+
//             |         |          +Z points into the page
//             | screen  |          (out the back of the device)
//             |         |
//             +---------+  --> +Y
//            bottom edge |
//                        v
//                       +X
//
// Accelerometer values are in m/s2 (1 g ~ 9.81). Gyroscope values are angular
// rate in degrees per second. The sensor reports raw, instantaneous readings:
// there is no sensor fusion, no orientation/attitude estimate and no position.
// Any such filtering (complementary, Mahony, Madgwick, ...) is up to the app.
//
// ---------------------------------------------------------------------------
// API reference
// ---------------------------------------------------------------------------
// All functions are declared by bsp/orientation.h (included above) and return
// ESP_OK on success or an ESP-IDF error code on failure.
//
//   esp_err_t bsp_orientation_enable_accelerometer(void);
//       Enable the accelerometer. Call once before reading accel data.
//
//   esp_err_t bsp_orientation_disable_accelerometer(void);
//       Disable the accelerometer (optional — power draw is negligible).
//
//   esp_err_t bsp_orientation_enable_gyroscope(void);
//       Enable the gyroscope. Call once before reading gyro data.
//
//   esp_err_t bsp_orientation_disable_gyroscope(void);
//       Disable the gyroscope (optional — power draw is negligible).
//
//   esp_err_t bsp_orientation_get(bool* out_gyro_ready, bool* out_accel_ready,
//                                 float* out_gyro_x, float* out_gyro_y, float* out_gyro_z,
//                                 float* out_accel_x, float* out_accel_y, float* out_accel_z);
//       Read the current sensor data. Any out_* pointer may be NULL if that
//       value is not needed. Parameters (all [out]):
//         out_gyro_ready   true if fresh gyroscope data was available
//         out_accel_ready  true if fresh accelerometer data was available
//         out_gyro_x       Gyroscope X axis (degrees per second)
//         out_gyro_y       Gyroscope Y axis (degrees per second)
//         out_gyro_z       Gyroscope Z axis (degrees per second)
//         out_accel_x      Accelerometer X axis (m/s2)
//         out_accel_y      Accelerometer Y axis (m/s2)
//         out_accel_z      Accelerometer Z axis (m/s2)
//       See the axis diagram above for the meaning of the X/Y/Z directions.
//
// ===========================================================================
