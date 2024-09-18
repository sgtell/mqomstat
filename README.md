# mqomstat
An MQTT daemon for the HAI omnistat family of smart thermostats

## Initial version
- opens one serial port, specified on the command line
- communicates with one HAI Omnistat family thermostat
- exposes an MQTT interface that is still being designed
- Tested so far with an RC-80

## Usage
   mqomstat [options] serial port
   Options:
     -a thermostat-address
     -n thermostat-name
     -v verbose
   Example:
	mqomstat /dev/ttyUSB0 -a 0x02 -n house-hvac -v

## Coming Soon
- Testing also with RC-2000
- Config file specifying multiple thermostats on the shared 300bps serial port,
 instead of only one specified on the command line


