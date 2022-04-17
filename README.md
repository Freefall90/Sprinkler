# About
This project is an ESP32 based WiFi sprinkler system controller. It uses MQTT (in conjunction with NodeRed and Homebridge) to allow a customizable interface to your sprinkler system. Because it's MQTT it should be adaptable to several platforms.

# Limitations
There is some hard coded stuff in here specific to Homebridge, but not much.
I'm using an import to get my wifi credentials. It won't build without you creating your own.

# Disclaimer
I really suck at C programming. There are probably better ways to do almost everything I've built here. *shrug*, it works.