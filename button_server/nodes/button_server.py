#!/usr/bin/python
# ROS specific imports
import roslib; roslib.load_manifest('button_server')
import rospy
import SimpleHTTPServer
import SocketServer
import std_msgs
import os
from string import Template

from button_server.ButtonServer import ButtonServer

if __name__ == '__main__':
    try:
        server = ButtonServer()
        server.run()
    except rospy.ROSInterruptException: 
        pass

