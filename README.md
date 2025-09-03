# mqomstat
An MQTT daemon for the HAI omnistat family of smart thermostats

## Initial version
- opens one serial port, specified on the command line
- communicates with one or more HAI Omnistat family thermostat
- exposes an MQTT interface	
- Tested so far with RC-80 and RC-2000

## Usage
   mqomstat [options] [serial port]
   Options:
     -a thermostat-address
     -n thermostat-name
     -c config-filename
     -v verbose
   Examples:
	mqomstat /dev/ttyUSB0 -a 0x02 -n house-hvac -v
	mqomstat -c mqoms.ini

## Coming Soon
- publish some status when we shutdown, so mqtt clients know the thermostats aren't available


## MQTT Topics

The MQTT topics are all of the form:
    omnistat/THERMOSTAT-NAME/topic-suffix
or
    omnistat/server/topic-suffix

Thermostat names are specified on the command line, or in the .ini file.

Topic suffixes are:

/model thermostat model number
       example model RC-80

cool_set       cooling setpoint temperature, degrees C

heat_set       heating setpoint temperature, degrees C

current	       current room temperature, degrees C

state	       Is the server in communication with the thermostat.
	       One of "alive", "dead"
	       
fanmode		system fan mode.  one of "auto" or "on",
		(RC-2000 cycle mode not supported yet)

holdmode       system hold mode.  one of "auto" or "hold"

tstatmode        (from register 0x3d)
		 commanded mode; one of "off", "heat", "cool", or "auto"

curmode	       current mode of the thermostat, from register 0x47.
	       This will roughly follow tstatmode.
	       If tstat mode is auto, thermostat will select heat or cool based on
	       current temperature.  emergency 
	       One of:
	     	       "off", "heat", "cool", "em-heat"

outstatus      current state of the thermostat's output wires from register 0x48.
	       what the thermostat is currently commanding the hvac system to do.
		  one or more words, separated by '|'
	"heat" or "cool"
		"run" if running
		"fan" if fan is on
		"em-heat" if emergency heat is on
		"s2" if stage 2 is running
