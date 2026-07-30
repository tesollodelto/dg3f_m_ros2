// Copyright 2025 TESOLLO
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//    * Redistributions of source code must retain the above copyright
//      notice, this list of conditions and the following disclaimer.
//
//    * Redistributions in binary form must reproduce the above copyright
//      notice, this list of conditions and the following disclaimer in the
//      documentation and/or other materials provided with the distribution.
//
//    * Neither the name of the TESOLLO nor the names of its
//      contributors may be used to endorse or promote products derived from
//      this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#pragma once

#include <cmath>
#include <cstddef>
#include <algorithm>
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include "dg3f_driver/modbusTCP.hpp"
// #include "dg3f_driver/dg5f_enum.hpp"

// Define a data structure for received data
struct ReceivedData {
    std::vector<double> position;
    std::vector<double> velocity;
    std::vector<double> current;
};

// Fix class implementation
class DG3F_TCP {
public:
  // Proper constructor implementation
  DG3F_TCP(const std::string& ip, uint16_t port)
    : ip_(ip), port_(port)
  {
    ModbusClient_ = std::make_unique<ModbusClient>(ip_, port_);
  }

  virtual ~DG3F_TCP() = default;

  bool connect() {
    return ModbusClient_->connect();
  }

  // Add missing get_data method
  ReceivedData get_data()
   {

    ReceivedData data;
    data.position = get_position_rad();
    data.current = get_current();
    data.velocity= get_velocity();  // Assuming current is same as position for this example
    // For velocity, you can either compute it or return zeros
    // data.velocity.resize(MOTOR_NUM, 0.0);?
    return data;
  }

  std::vector<double> get_position_rad() {
    std::vector<double> positions(MOTOR_NUM);
    std::vector<int16_t> signed_positions;
    
    signed_positions = ModbusClient_->readInputRegisters(MOTOR1_CURRENT_POSITION, MOTOR_NUM);
    
    for (int i = 0; i < MOTOR_NUM; i++) {
      positions[i] = static_cast<double>(signed_positions[i]) * RAD_SCALE;
    }
    
    return positions;
  }

  void start_control() {
    // std::vector<uint16_t> control_start(MOTOR_NUM, 1);
    ModbusClient_->writeSingleRegister(0, 1);
  }
  std::vector<double> get_current() {
    std::vector<double> currents(MOTOR_NUM);
    std::vector<int16_t> current_values;
    
    current_values = ModbusClient_->readInputRegisters(MOTOR1_CURRENT_CURRENT, MOTOR_NUM);
    
    for (int i = 0; i < MOTOR_NUM; i++) {
      currents[i] = static_cast<double>(current_values[i]) * CURRENT_SCALE;
    }
    
    return currents;
  }

  std::vector<double> get_velocity() {
    std::vector<double> velocities(MOTOR_NUM);
    std::vector<int16_t> velocity_values;
    
    velocity_values = ModbusClient_->readInputRegisters(MOTOR1_CURRENT_VELOCITY, MOTOR_NUM);
    
    for (int i = 0; i < MOTOR_NUM; i++) {
      velocities[i] = static_cast<double>(velocity_values[i]) * VELOCITY_SCALE;
    }
    
    return velocities;
  }
  
  // Holding Register MOTOR1_TARGET_POSITION + i holds motor i+1's target
  // position, x10 (0.1 deg per LSB) with a valid range of -1800..1800. Writing
  // more than MOTOR_NUM values runs off the end of this model's target block
  // into the neighbouring motors' targets and then, from HR 27, into the
  // target-position reach times, so the count is clamped here.
  bool set_position_rad(const std::vector<double>& positions) {
    const std::size_t count =
        std::min<std::size_t>(positions.size(), static_cast<std::size_t>(MOTOR_NUM));
    if (positions.size() != static_cast<std::size_t>(MOTOR_NUM)) {
      std::cerr << "set_position_rad: received " << positions.size()
                << " values for " << MOTOR_NUM << " motors; using " << count
                << std::endl;
    }
    if (count == 0) {
      return false;
    }

    std::vector<uint16_t> position_int(count);
    for (std::size_t i = 0; i < count; ++i) {
      // The register is a signed 16-bit value; a plain cast to uint16_t turned
      // a legal negative target into ~65000 and drove the joint to the
      // opposite limit.
      double raw = std::round(positions[i] / RAD_SCALE);
      if (raw < POSITION_TARGET_MIN) raw = POSITION_TARGET_MIN;
      if (raw > POSITION_TARGET_MAX) raw = POSITION_TARGET_MAX;
      position_int[i] =
          static_cast<uint16_t>(static_cast<int16_t>(static_cast<int>(raw)));
    }

    ModbusClient_->writeMultiRegisters(MOTOR1_TARGET_POSITION, position_int);
    return true;
  }

  bool isConnected() const {
    return ModbusClient_ && ModbusClient_->isConnected();
  }

private:
  std::string ip_;
  uint16_t port_;
  std::unique_ptr<ModbusClient> ModbusClient_;
  std::mutex mutex_;
  
  // Constants (adjust as needed)
  static constexpr int MOTOR_NUM = 12;
  static constexpr double RAD_SCALE = M_PI / 1800.0;
  // Control Manual: 모터 N 위치 타겟값 [-1800 to 1800] (x10 => 0.1 deg/LSB)
  static constexpr int POSITION_TARGET_MIN = -1800;
  static constexpr int POSITION_TARGET_MAX = 1800;
  static constexpr double VELOCITY_SCALE = M_PI/30.0;
  static constexpr double CURRENT_SCALE = 0.001;
  static constexpr int MOTOR1_CURRENT_POSITION = 6;
  static constexpr int MOTOR1_TARGET_POSITION = 7;
  static constexpr int MOTOR1_CURRENT_CURRENT = 26;
  static constexpr int MOTOR1_CURRENT_VELOCITY = 46;
};
