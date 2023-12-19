
#include <laam_laser_control/pid.h>

using namespace pid_ns;

//PidObject::PidObject() : error_(3, 0), filtered_error_(3, 0), error_deriv_(3, 0), filtered_error_deriv_(3, 0)     
PidObject::PidObject() : error_(3, 0)
{
  ros::NodeHandle node;                                                                                       
  ros::NodeHandle node_priv("~");                                                                             
  while (ros::ok() && ros::Time(0) == ros::Time::now())
  {      
    ROS_INFO("controller spinning, waiting for time to become non-zero");
    sleep(1);
  }

  // Get params if specified in launch file or as params on command-line, set
  // defaults
  node_priv.param<double>("Kp", Kp_, 0.7);                                                              
  node_priv.param<double>("Ki", Ki_, 0.1);
  //node_priv.param<double>("Kd", Kd_, 0.0);                                                                      
  node_priv.param<double>("upper_limit", upper_limit_, 0.05);
  node_priv.param<double>("lower_limit", lower_limit_, 0.0);
  node_priv.param<double>("windup_limit", windup_limit_, 1000);
  node_priv.param<double>("cutoff_frequency", cutoff_frequency_, -1.0);          
  node_priv.param<std::string>("topic_from_control_", topic_from_control_, "control/power");                       
  node_priv.param<std::string>("setpoint_topic", setpoint_topic_, "control/setpoint");                         
  node_priv.param<std::string>("pid_enable_topic", pid_enable_topic_, "pid_enable");
  node_priv.param<double>("max_loop_frequency", max_loop_frequency_, 1.0);                                      
  node_priv.param<double>("min_loop_frequency", min_loop_frequency_, 1000.0);
  node_priv.param<std::string>("pid_debug_topic", pid_debug_pub_name_, "pid_debug");
  node_priv.param<double>("setpoint_timeout", setpoint_timeout_, -1.0);
  ROS_ASSERT_MSG(setpoint_timeout_ ==-1 || setpoint_timeout_ > 0, 
                 "setpoint_timeout set to %.2f but needs to -1 or >0", setpoint_timeout_);
 
  // Two parameters to allow for error calculation with discontinous value
  node_priv.param<bool>("rms_energy_error", rms_energy_error_, true);
  //node_priv.param<double>("angle_wrap", angle_wrap_, 2.0 * 3.14159);                                
  node_priv.param<std::string>("rms_energy", rms_energy_topic, "acoustic_feature");                                              


  // Update params if specified as command-line options, & print settings
  printParameters();
  if (not validateParameters())
    std::cout << "Error: invalid parameter\n";


  // instantiate publishers & subscribers                         
  control_effort_pub_ = node.advertise<laam_laser_control::MsgPower>(topic_from_control_, 1);                                                  
  pid_debug_pub_ = node.advertise<std_msgs::Float64MultiArray>(pid_debug_pub_name_, 1);
  setpoint_pub_ = node.advertise<laam_laser_control::MsgSetpoint>(setpoint_topic_, 1);
        
  ros::Subscriber pid_enabled_sub_ = node.subscribe(pid_enable_topic_, 1, &PidObject::pidEnableCallback, this);                                                                     //fn
  ros::Subscriber rms_sub_ = node.subscribe(rms_energy_topic, 1, &PidObject::plantStateCallback, this);               


  if (!pid_enabled_sub_ || !rms_sub_)
  {
    ROS_ERROR_STREAM("Initialization of a subscriber failed. Exiting.");
    ros::shutdown();
    exit(EXIT_FAILURE);
  }

  // dynamic reconfiguration
  dynamic_reconfigure::Server<pid::PidConfig> config_server;
  dynamic_reconfigure::Server<pid::PidConfig>::CallbackType f;
  f = boost::bind(&PidObject::reconfigureCallback, this, _1, _2);
  config_server.setCallback(f);


  // Wait for first messages 
  while( ros::ok() && !ros::topic::waitForMessage<acoustic_monitoring_msgs::MsgAcousticFeature>(rms_energy_topic, ros::Duration(10.)))
     ROS_WARN_STREAM("Waiting for first state message from the plant.");

  // Respond to inputs until shut down
  while (ros::ok())
  {
    doCalcs();                                                                                
    
    ros::spinOnce();

    // Add a small sleep to avoid 100% CPU usage
    ros::Duration(0.001).sleep();
  }
};


void PidObject::setpointRelease()
  {
    rms_energy_list.push_back(plant_state_);
    setpoint_.setpoint = accumulate(rms_energy_list.begin(), rms_energy_list.end(), 0.0) / rms_energy_list.size();
    setpoint_pub_.publish(setpoint_);
    new_state_or_setpt_ = true;
}


void PidObject::plantStateCallback(const acoustic_monitoring_msgs::MsgAcousticFeature& MsgAcousticFeature)
{
  plant_state_ = MsgAcousticFeature.rms_energy;                
  new_state_or_setpt_ = true;
}


void PidObject::pidEnableCallback(const std_msgs::Bool& pid_enable_msg)
{
  pid_enabled_ = pid_enable_msg.data;
}

void PidObject::getParams(double in, double& value, double& scale)
{
  int digits = 0;
  value = in;
  while (ros::ok() && ((fabs(value) > 1.0 || fabs(value) < 0.1) && (digits < 2 && digits > -1)))
  {
    if (fabs(value) > 1.0)
    {
      value /= 10.0;
      digits++;
    }
    else
    {
      value *= 10.0;
      digits--;
    }
  }
  if (value > 1.0)
    value = 1.0;
  if (value < -1.0)
    value = -1.0;

  scale = pow(10.0, digits);
}

bool PidObject::validateParameters()
{
  if (lower_limit_ > upper_limit_)
  {
    ROS_ERROR("The lower saturation limit cannot be greater than the upper "
              "saturation limit.");
    return (false);
  }

  return true;
}

void PidObject::printParameters()
{
  std::cout << std::endl << "PID PARAMETERS" << std::endl << "-----------------------------------------" << std::endl;
  std::cout << "Kp: " << Kp_ << ",  Ki: " << Ki_ << std::endl;
  if (cutoff_frequency_ == -1)  // If the cutoff frequency was not specified by the user
    std::cout << "LPF cutoff frequency: 1/4 of sampling rate" << std::endl;
  else
    std::cout << "LPF cutoff frequency: " << cutoff_frequency_ << std::endl;
    std::cout << "pid node name: " << ros::this_node::getName() << std::endl;
    std::cout << "Name of topic from controller: " << topic_from_control_ << std::endl;
    std::cout << "Name of topic from the plant: " << rms_energy_topic << std::endl;
    std::cout << "Name of setpoint topic: " << setpoint_topic_ << std::endl;
    std::cout << "Integral-windup limit: " << windup_limit_ << std::endl;
    std::cout << "Saturation limits: " << upper_limit_ << "/" << lower_limit_ << std::endl;
    std::cout << "-----------------------------------------" << std::endl;

  return;
}

void PidObject::reconfigureCallback(pid::PidConfig& config, uint32_t level)
{
  if (first_reconfig_)
  {
    getParams(Kp_, config.Kp, config.Kp_scale);
    getParams(Ki_, config.Ki, config.Ki_scale);
    //getParams(Kd_, config.Kd, config.Kd_scale);
    first_reconfig_ = false;
    return;  // Ignore the first call to reconfigure which happens at startup
  }

  Kp_ = config.Kp * config.Kp_scale;
  Ki_ = config.Ki * config.Ki_scale;
  //Kd_ = config.Kd * config.Kd_scale;
  ROS_INFO("Pid reconfigure request: Kp: %f, Ki: %f", Kp_, Ki_);
}




void PidObject::doCalcs()
{
  // Do fresh calcs if knowledge of the system has changed.
  if (new_state_or_setpt_)
  {
    if (!((Kp_ <= 0. && Ki_ <= 0. ) ||
          (Kp_ >= 0. && Ki_ >= 0. )))  // All 3 gains should have the same sign
      ROS_WARN("All three gains (Kp, Ki, Kd) should have the same sign for "
               "stability.");

    //  how values are "appended" instead of using vector.push_back -> will generate error
    error_.at(2) = error_.at(1);
    error_.at(1) = error_.at(0);
    error_.at(0) = setpoint_.setpoint - plant_state_;  // Current error goes to slot 0

    
    // calculate delta_t
    if (!prev_time_.isZero())  // Not first time through the program since prev_time is not 0
    {
      delta_t_ = ros::Time::now() - prev_time_;
      prev_time_ = ros::Time::now();
      if (0 == delta_t_.toSec())
      {
        ROS_ERROR("delta_t is 0, skipping this loop. Possible overloaded cpu "
                  "at time: %f",
                  ros::Time::now().toSec());
        return;
      }
    }

    else                                             //first time program runs
    {
      ROS_INFO("prev_time is 0, doing nothing");
      prev_time_ = ros::Time::now();                                    
      return;
    }

    // Integral of error
    // Bi-linear (Tustin, Trapezoidal) technique to transform integral control to z-domain / discrete domain. 
    // Transforming to discrete domain allows better performance compared to Euler (Forward) and Backward rule, which is commonly used in time-domain integral control.
    error_integral_ += 0.5 * (error_.at(0) + error_.at(1)) * delta_t_.toSec();                                               

    // Apply windup limit to limit the size of the integral term. Windup limit should be adjusted accordingly to ensure stability of system.
    if (error_integral_ > fabsf(windup_limit_))                                                       
      error_integral_ = fabsf(windup_limit_);                                                         

    if (error_integral_ < -fabsf(windup_limit_))
      error_integral_ = -fabsf(windup_limit_);  

    // 2nd order LP filter and derivative error calculation
    
    /*
    // My filter reference was Julius O. Smith III, Intro. to Digital Filters
    // With Audio Applications.
    // See https://ccrma.stanford.edu/~jos/filters/Example_Second_Order_Butterworth_Lowpass.html
    if (cutoff_frequency_ != -1)
    {
      // Check if tan(_) is really small, could cause c = NaN
      tan_filt_ = tan((cutoff_frequency_ * 6.2832) * delta_t_.toSec() / 2);

      // Avoid tan(0) ==> NaN
      if ((tan_filt_ <= 0.) && (tan_filt_ > -0.01))
        tan_filt_ = -0.01;
      if ((tan_filt_ >= 0.) && (tan_filt_ < 0.01))
        tan_filt_ = 0.01;

      c_ = 1 / tan_filt_;
    }

   
    filtered_error_.at(2) = filtered_error_.at(1);
    filtered_error_.at(1) = filtered_error_.at(0);
    filtered_error_.at(0) = (1 / (1 + c_ * c_ + 1.414 * c_)) * (error_.at(2) + 2 * error_.at(1) + error_.at(0) -
                                                                (c_ * c_ - 1.414 * c_ + 1) * filtered_error_.at(2) -
                                                                (-2 * c_ * c_ + 2) * filtered_error_.at(1));
   


    // Take derivative of error
    // First the raw, unfiltered data:
    error_deriv_.at(2) = error_deriv_.at(1);
    error_deriv_.at(1) = error_deriv_.at(0);
    error_deriv_.at(0) = (error_.at(0) - error_.at(1)) / delta_t_.toSec();                              

    filtered_error_deriv_.at(2) = filtered_error_deriv_.at(1);
    filtered_error_deriv_.at(1) = filtered_error_deriv_.at(0);

    filtered_error_deriv_.at(0) =
        (1 / (1 + c_ * c_ + 1.414 * c_)) *
        (error_deriv_.at(2) + 2 * error_deriv_.at(1) + error_deriv_.at(0) -
         (c_ * c_ - 1.414 * c_ + 1) * filtered_error_deriv_.at(2) - (-2 * c_ * c_ + 2) * filtered_error_deriv_.at(1));

    */


    // Calculate the control effort

    proportional_ = Kp_ * error_.at(0);
    integral_ = Ki_ * error_integral_;         
    //integral_ = Ki_ * error_integral_ * delta_t_.toSec();
    control_effort_ += (proportional_ + integral_);


    // Apply saturation limits
    if (control_effort_ > upper_limit_)
      control_effort_ = upper_limit_;
    else if (control_effort_ < lower_limit_)                                    
      control_effort_ = lower_limit_;

    // Publish the stabilizing control effort if the controller is enabled
    if (pid_enabled_ && (setpoint_timeout_ == -1 || 
                         (ros::Time::now() - last_setpoint_msg_time_).toSec() <= setpoint_timeout_))
    
    { 
      setpointRelease();
      std::cout << "-----------------------------------------" << control_effort_<<std::endl;
      power_value_.value = control_effort_;                  
      control_effort_pub_.publish(power_value_);
    


      // Publish topic with
      std::vector<double> pid_debug_vect { plant_state_, control_effort_, proportional_, integral_};
      std_msgs::Float64MultiArray pidDebugMsg;
      pidDebugMsg.data = pid_debug_vect;
      pid_debug_pub_.publish(pidDebugMsg);
    }
    else if (setpoint_timeout_ > 0 && (ros::Time::now() - last_setpoint_msg_time_).toSec() > setpoint_timeout_)
    {
      ROS_WARN_ONCE("Setpoint message timed out, will stop publising control_effort_messages");
      error_integral_ = 0.0;
    } 
    else
      error_integral_ = 0.0;
  }

  new_state_or_setpt_ = false;
}
