/***************************************************************************/ /**
 * \file controller.h
 *
 * \brief Simple PID controller with dynamic reconfigure
 * \author Andy Zelenak
 * \date March 8, 2015
 *
 * \section license License (BSD-3)
 * Copyright (c) 2015, Andy Zelenak\n
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * - Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * - Neither the name of Willow Garage, Inc. nor the names of its contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 ******************************************************************************/

#ifndef PID_H
#define PID_H

#include "ros/ros.h"
#include <dynamic_reconfigure/server.h>
#include <iostream>
#include <laam_laser_control/PidConfig.h>
#include <ros/time.h>
#include <std_msgs/Bool.h>
#include <std_msgs/Float64.h>
#include <std_msgs/Float64MultiArray.h>
#include <stdio.h>
#include <string>
#include <laam_laser_control/MsgSetpoint.h>
#include <laam_laser_control/MsgPower.h>
#include <acoustic_monitoring_msgs/MsgAcousticFeature.h>
#include <numeric>

    
// Defining pid_ns
namespace pid_ns
{
class PidObject
{
public:
  PidObject();
  

private:
  void doCalcs();
  void getParams(double in, double& value, double& scale);
  void pidEnableCallback(const std_msgs::Bool& pid_enable_msg);
  void plantStateCallback(const acoustic_monitoring_msgs::MsgAcousticFeature& MsgAcousticFeature);      
  void reconfigureCallback(pid::PidConfig& config, uint32_t level);
  void setpointRelease();
  //void adaptive_setpt(const ros::Time& current_time, double plant_state);         adaptive_setpt() was not used as the function is still unstable
  void printParameters();
  bool validateParameters(); 

  // Primary PID controller input variables
  double plant_state_;               // current output of plant
  bool pid_enabled_ = true;          // PID is enabled to run
  bool new_state_or_setpt_ = false;  // Indicate that fresh calculations need to be run
  double ADAPTIVE_SETPOINT_INTERVAL = 10.0;
  ros::Time current_time;
  
  ros::Time prev_time_;
  ros::Time last_setpoint_msg_time_;
  ros::Duration delta_t_;
  bool first_reconfig_ = true;
  ros::Time adaptive_time;


  double error_integral_ = 0;
  double proportional_ = 0;  // proportional term of output
  double integral_ = 0;      // integral term of output
  

  // PID gains
  double Kp_ = 0, Ki_ = 0;

  // Parameters for error calc. with disconinuous input 
  //bool angle_error_ = false;
  bool rms_energy_error_ = true;
  double value;

  // Cutoff frequency for the derivative calculation in Hz.
  // Negative -> Has not been set by the user yet, so use a default.
  double cutoff_frequency_ = -1;
  
  // Setpoint timeout parameter to determine how long to keep publishing
  // control_effort messages after last setpoint message
  // -1 indicates publish indefinately, and positive number sets the timeout
  double setpoint_timeout_ = -1;
  double control_effort_ = 0;        // output of pid controller


  // 2nd order Lowpass filter

  /*
  // Used in filter calculations. Default 1.0 corresponds to a cutoff frequency
  // at
  // 1/4 of the sample rate.
  double c_ = 1.;

  // Used to check for tan(0)==>NaN in the filter calculation
  double tan_filt_ = 1.;
  */


  // Upper and lower saturation limits
  double upper_limit_ = 3000, lower_limit_ = 0;

  // Anti-windup term. Limits the absolute value of the integral term.
  double windup_limit_ = 1000;

  // Initialize arrays for data storage.                                             
  //std::vector<double> error_, filtered_error_, error_deriv_, filtered_error_deriv_;
  std::vector<double> error_, rms_energy_list;
 

  // Topic and node names and message objects
  ros::Publisher control_effort_pub_;
  ros::Publisher pid_debug_pub_;
  ros::Publisher setpoint_pub_;
  

  std::string topic_from_control_, topic_from_plant_, setpoint_topic_, pid_enable_topic_, rms_energy_topic;
  std::string pid_debug_pub_name_;

  // Initialising Message Files
  laam_laser_control::MsgSetpoint setpoint_; 
  acoustic_monitoring_msgs::MsgAcousticFeature msg_acoustic_feature;    //just initialisr msg_acoustic_feature although you won't be using it, this is so that you can import it as .h file
  laam_laser_control::MsgPower power_value_;

  // Diagnostic objects
  double min_loop_frequency_ = 1, max_loop_frequency_ = 1000;
  int measurements_received_ = 0;
};
}  // end pid namespace

#endif
