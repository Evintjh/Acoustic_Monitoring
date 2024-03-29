#!/usr/bin/env python
import os
import rospy
import rospkg

from laam_laser_control.msg import MsgControl
from laam_laser_control.msg import MsgPower
from laam_laser_control.msg import MsgInfo
from laam_laser_control.msg import MsgStart
from laam_laser_control.msg import MsgSetpoint
from laam_laser_control.msg import MsgStatus

#from audio_common_msgs.msg import AudioData
#from audio_common_msgs.msg import AudioInfo
from acoustic_monitoring_msgs.msg import (
    AudioDataStamped,
    MsgAcousticFeature)
#from camera_monitoring.msg import MsgGeometry                           
#from camera_monitoring.msg import MsgStatus                                    ####
# from camera_monitoring.msg import MsgVelocityStatus

from control.control import Control
from control.control import PID
import numpy as np

from threading import Timer,Thread,Event,Lock
import time
import logging


MANUAL = 0
STEP = 2
AUTOMATIC = 1
ADAPTIVE_SETPOINT_INTERVAL = 50 # adaptive setpoint interval [sec]

class Timer_thread(Thread):
    def __init__(self, interval, hFunction):
        Thread.__init__(self)
        self.hFunction = hFunction
        self.interval = interval
        
        # initialize a timer object to execute the function handler
        # this timer will call function handler after [interval] seconds
        self.thread = Timer(self.interval,self.function_handler)
        
    def function_handler(self):
        self.hFunction() # execute the function (the actual function for execution)
        # create a new timer to call the funciton_handler again
        self.thread = Timer(self.interval,self.function_handler)
        self.thread.start()
    
    def start(self):
        self.thread.start() # this will start the first timer
        
    def cancel(self):
        self.thread.cancel() # cancel the timer thread



class NdControl():
    def __init__(self):
        rospy.init_node('control')

        #audio_topic_info = rospy.get_param('~audio_topic_info', '/audio_info')

        # subscribe the audio topic, and use callback function to visualise and post-process it. topic: filteredAudioStamped
        #rospy.Subscriber('audioStamped', AudioDataStamped, self.cb_acoustic_signal, queue_size=2)
        #rospy.Subscriber(audio_topic_info, AudioInfo, self.cb_audio_info, queue_size=1)
       # rospy.Subscriber('audioStamped', AudioDataStamped, self.cb_audio_data, queue_size=2)
        
        '''
        rospy.Subscriber(
            '/usb_cam/geometry', MsgGeometry, self.cb_geometry, queue_size=1)       
        '''
        rospy.Subscriber(
            '/supervisor/status', MsgStatus, self.cb_status, queue_size=1)
                                
        rospy.Subscriber(
            '/control/parameters', MsgControl, self.cb_control, queue_size=1)
        rospy.Subscriber(
            '/acoustic_feature', MsgAcousticFeature, self.cb_rms, queue_size=1)
        # rospy.Subscriber(
        #     '/supervisor/velocity_status', MsgVelocityStatus, self.cb_status, queue_size=1)
        self.pub_power = rospy.Publisher(
            '/control/power', MsgPower, queue_size=10)
        self.pub_info = rospy.Publisher(
            '/control/info', MsgInfo, queue_size=10)
        self.pub_start = rospy.Publisher(
            '/control/start', MsgStart, queue_size=10)
        self.pub_setpoint = rospy.Publisher(
            '/control/setpoint', MsgSetpoint, queue_size=10)
        
        


        self.msg_power = MsgPower()
        self.msg_info = MsgInfo()
        self.msg_start = MsgStart()
        self.msg_setpoint = MsgSetpoint()
        self.msg_acoustic_feature = MsgAcousticFeature()
        self.msg_control = MsgControl()
        self.msg_status = MsgStatus()
        self.mode = MANUAL

        self.status = False

        self.time_step = float(0)
        self.step_increase = 100
        self.control = Control()
        self.updateParameters()
        self.start = False

        self.track= []
        
        #self.rms_energy = 0
        self.rms_energy_list = []
        self.adaptive_time = 0.0

        self.track_number = 0
        self.t_reg = 0
        self.time_control = 0
        self.track_control = 3
        self.auto_mode = 0
        self.control_time_interval = 0
        self.power_ant = 0
        self.setFirstPowerValue = True

        self.setPowerParameters(rospy.get_param('/control_parameters/power'))
        self.control.pid.set_limits(self.power_min, self.power_max)
        self.control.pid.set_setpoint(self.setpoint)
        
        # timer_reduce_power = Timer_thread(5,self.reduce_power_level) # wait 30s to execute function once 
        # timer_reduce_power.start() # start the thread

        rospy.spin()
    
    

    # def cb_audio_data(self, msg_audio):
    #     audio_data = msg_audio.data
    #     audio_data_numpy = np.frombuffer(audio_data, dtype=np.int16)

    def reduce_power_level(self):
        if self.power_max < 5.5 or self.power_min < 4.7: 
            pass
        else:
            self.power_max -= 0.01
            self.power_min -= 0.01
    
    def setParameters(self, params):
        self.Kp = params['Kp']
        self.Ki = params['Ki']
        self.Kd = params['Kd']
        self.control.pid.set_parameters(self.Kp, self.Ki, self.Kd)
        # print 'Kp:', self.Kp, 'Ki:', self.Ki, 'Kd', self.Kd

    def setManualParameters(self, params):
        self.power = params['power']

    def setAutoParameters(self, params):
        self.setpoint = params['width']             #width???
        self.control_time_interval = params['time']
        self.auto_mode = params['mode']

    def setPowerParameters(self, params):
        self.power_min = params['min']
        self.power_max = params['max']

    def setStepParameters(self, params):
        self.power_step = params['power']
        self.trigger = params['trigger']

    def updateParameters(self):
        # get the ros parameter, which is set from 'control_parameters.yaml' file
        self.setParameters(rospy.get_param('/control_parameters/pid_parameters')) 
        self.setStepParameters(rospy.get_param('/control_parameters/step'))
        self.setManualParameters(rospy.get_param('/control_parameters/manual'))
        self.setAutoParameters(rospy.get_param('/control_parameters/automatic'))

    def cb_control(self, msg_control):
        self.mode = msg_control.value # mode set by qt/ Manual/auto/step
        self.updateParameters()
        # self.t_auto = 0.01* self.control_time_interval
        # self.t_reg = 0.005* self.control_time_interval 
        self.track_number = 0
        self.time_step= 0
        self.control.pid.set_setpoint(self.setpoint)
        self.adaptive_time = rospy.get_time() # get current ros time in sec
        #-----------------------------------------
        self.control_change = msg_control.change
    
    def cb_status(self, msg_status):
        self.power_ant = msg_status.power
        if msg_status.laser_on and not self.status:
            self.time_step = 0
            self.track_number += 1
        self.status = msg_status.laser_on
    

    def cb_rms(self, msg_acoustic_feature):
        stamp = msg_acoustic_feature.header.stamp
        time = stamp.to_sec()
        #self.rms_energy = self.msg_acoustic_feature.rms_energy
        if self.mode == MANUAL:
            self.setFirstPowerValue = True
            value = self.manual(self.power)
        elif self.mode == AUTOMATIC:
            # value = self.automatic(msg_geo.minor_axis, time)
            value = self.automatic(msg_acoustic_feature.spectral_centroids[0], time)
        elif self.mode == STEP:
            value = self.step(time)
        value = self.range(value)
        # value = self.cooling(msg_geo.minor_axis, value) 
        self.msg_power.header.stamp = stamp
        self.msg_power.value = value
        self.msg_info.time = str(self.time_control)
        self.msg_info.track_number = self.track_number
        rospy.set_param('/control/power', value)
        self.pub_power.publish(self.msg_power)
        self.pub_info.publish(self.msg_info)
        #self.pub_rms_energy.publish(msg_acoustic_feature)

        start = self.msg_start.control
        if start is not self.start: # if this is the first time control
            self.control.pid.set_setpoint(self.setpoint)
            self.msg_start.setpoint = self.setpoint
            self.pub_start.publish(self.msg_start)
        self.start = start


    '''
    def cb_geometry(self, msg_geo):
        stamp = msg_geo.header.stamp
        time = stamp.to_sec()
        if self.mode == MANUAL:
            self.setFirstPowerValue = True
            value = self.manual(self.power)
        elif self.mode == AUTOMATIC:
            # value = self.automatic(msg_geo.minor_axis, time)
            value = self.automatic(msg_geo.minor_axis_average, time)
        elif self.mode == STEP:
            value = self.step(time)
        value = self.range(value)
        # value = self.cooling(msg_geo.minor_axis, value) 
        self.msg_power.header.stamp = stamp
        self.msg_power.value = value
        self.msg_info.time = str(self.time_control)
        self.msg_info.track_number = self.track_number
        rospy.set_param('/control/power', value)
        self.pub_power.publish(self.msg_power)
        self.pub_info.publish(self.msg_info)
        start = self.msg_start.control
        if start is not self.start: # if this is the first time control
            self.control.pid.set_setpoint(self.setpoint)
            self.msg_start.setpoint = self.setpoint
            self.pub_start.publish(self.msg_start)
        self.start = start
    '''

    def manual(self, power):
        value = self.control.pid.power(power)
        return value

    def automatic(self, rms_energy, time):
        # if self.time_step == 0:
        #     self.time_step = time
        #     self.control.pid.time = time
        # self.time_control= time - self.time_step
        # value = self.power
        # self.msg_start.control = False
        # if self.status and self.time_step > 0:
        #     if self.auto_mode is 0:    # continous mode, use time
        #         if self.time_control > self.t_reg and self.time_control < self.t_auto:
        #             self.auto_setpoint(minor_axis)
        #         if self.time_control > self.t_auto:
        #             value = self.control.pid.update(minor_axis, time)
        #             self.msg_start.control = True
        #     elif self.auto_mode is 1:   # use track number
        #         if self.track_number is 3:
        #             self.auto_setpoint(minor_axis)
        #         if self.track_number >= 4:
        #             value = self.control.pid.update(minor_axis, time)
        #             self.msg_start.control = True
        # return value
        
        if self.setFirstPowerValue:
            self.controlled_value = self.power
            self.setFirstPowerValue = False

        if self.time_step == 0:
            self.time_step = time
            self.control.pid.time = time
        self.time_control= time - self.time_step
        # value = self.power
        self.msg_start.control = False
        if self.status and self.time_step > 0:
            if self.auto_mode is 0 and rms_energy > 1000:    # continous mode, use time
                # if self.time_control < self.control_time_interval:
                #     self.auto_setpoint(minor_axis)
                # if self.time_control > self.control_time_interval:
                
                # self.adaptive_setpoint(time, minor_axis)
                # thread = Thread(target=self.adaptive_setpoint,
                #                 args=(time, minor_axis)
                #                 )
                # thread.start()
                
                self.controlled_value = self.control.pid.update(rms_energy, time)
                self.msg_start.control = True
            elif self.auto_mode is 1:   # use track number
                if self.track_number is 3:
                    self.auto_setpoint(rms_energy)
                if self.track_number >= 4:
                    self.controlled_value = self.control.pid.update(rms_energy, time)
                    self.msg_start.control = True
        
        value = self.controlled_value
        return value
    
    
    def adaptive_setpoint(self, current_time, rms_energy):
        countLock = Lock() # use Lock() to avoid conflict when multiple thread accessing the same vriable
        countLock.acquire()
        
        if current_time - self.adaptive_time < ADAPTIVE_SETPOINT_INTERVAL: 
            self.rms_energy_list.append(rms_energy)
        else:
            self.setpoint = sum(self.rms_energy_list)/len(self.rms_energy_list)
            self.control.pid.set_setpoint(self.setpoint)
            self.msg_setpoint.setpoint = self.setpoint
            self.pub_setpoint.publish (self.msg_setpoint)
            self.rms_energy_list = []
            self.adaptive_time = current_time
            
        countLock.release()
    
    


    def auto_setpoint(self, rms_energy):
        self.track.append(rms_energy)
        self.setpoint = sum(self.track)/len(self.track)

    def step(self, time):
        #Step time
        if self.time_step == 0:
            self.time_step = time
        if self.status and self.time_step > 0 and time - self.time_step > self.trigger:
            value = self.power_step
        else:
            value = self.power
        return value

    def range(self, value):
        if value < self.power_min:
            value = self.power_min
        elif value > self.power_max:
            value = self.power_max
        #if  not self.status:
        #    value = 0
        return value

    # def cooling(self, msg_geo, value):
    #     if msg_geo > 180:
    #         value = 0
    #     return value
    #     #avoiding overheating


if __name__ == '__main__':
    try:
        NdControl()
    except rospy.ROSInterruptException:
        pass