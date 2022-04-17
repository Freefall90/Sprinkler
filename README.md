# About
This project is an ESP32 based WiFi sprinkler system controller. It uses MQTT (in conjunction with NodeRed and Homebridge) to allow a customizable interface to your sprinkler system. Because it's MQTT it should be adaptable to several platforms.

It's written in C using the Espressif IOT Dev Framework.
I do not plan on porting this to Arduino. PRs welcome. I dislike the Arduino toolset, especially for ESP32 projects.

I may, at some point, commit my NodeRed flows. But they are incredibly simple and only exist to make things readable in Homebridge and at the same time be easy to parse in the C code. Mostly the flow consists of a function node to translate data types (because it's easier in JS than in C, for me).

# Limitations
There is some hard coded stuff in here specific to Homebridge, but not much.
I'm using an import to get my wifi credentials. It won't build without you creating your own.

# Disclaimer
C is not my strong suit. There are probably better ways to do almost everything I've built here. *shrug*, it works.