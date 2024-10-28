/*
 * omnistat wire-protocol stuff; mostly independent of how we structure
 * the communication.  mostly unchanged from previous code.
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <errno.h>
#include <glib.h>

#include "omnistat.h"

int omst_celsius = 1;

/*
 * Register names, read/write classification,
 * and appropriate byte <-> string conversion routines
 */
struct omst_reg rc8x_regs[] = {
	{ "address",              ROK, omcs_int,  omcb_int  }, /* 0 */
	{ "comm mode",            ROK, omcs_int,  omcb_int  }, /* 1 */
	{ "sys options",          ROK, omcs_int,  omcb_int  }, /* 2 */
	{ "display options",    SV|OK, omcs_int,  omcb_int  }, /* 3 */
	{ "temp offset" ,       SV|OK, omcs_tcal, omcb_tcal }, /* 4 */
	{ "cool limit",         SV|OK, omcs_temp, omcb_temp }, /* 5 */
	{ "heat limit",         SV|OK, omcs_temp, omcb_temp }, /* 6 */
	{ "reserved",            RESV, omcs_int,  NULL      }, /* 7 */
	{ "reserved",            RESV, omcs_int,  NULL      }, /* 8 */
	{ "cool anticipator",   SV|OK, omcs_int,  omcb_int  }, /* 09 */
	{ "heat anticipator",   SV|OK, omcs_int,  omcb_int  }, /* 0A */
	{ "cool cycle time",    SV|OK, omcs_int,  omcb_int  }, /* 0B */
	{ "heat cycle time",    SV|OK, omcs_int,  omcb_int  }, /* 0C */
	{ "aux heat diff",      SV|OK, omcs_int,  omcb_int  }, /* 0D */
	{ "clock adjust",       SV|OK, omcs_ccal, omcb_ccal }, /* 0E */
	{ "filter days left",      OK, omcs_int,  omcb_int  }, /* 0F */

	{ "hours run this week",   OK, omcs_int, omcb_int }, /* 10 */
	{ "hours run last week",   OK, omcs_int, omcb_int }, /* 11 */
	{ "RTP setback",         SV|OK, omcs_int, omcb_int }, /* 12 */
	{ "RTP high",            SV|OK, omcs_int, omcb_int }, /* 13 */
	{ "RTP crit",            SV|OK, omcs_int, omcb_int }, /* 14 */

	{ "weekday morn time",  SV|OK, omcs_ptime, omcb_ptime }, /* 15 */
	{ "weekday morn cool",  SV|OK, omcs_temp,  omcb_temp  }, /* 16 */
	{ "weekday morn heat",  SV|OK, omcs_temp,  omcb_temp  }, /* 17 */
	{ "weekday day time",   SV|OK, omcs_ptime, omcb_ptime }, /* 18 */
	{ "weekday day cool",   SV|OK, omcs_temp,  omcb_temp  }, /* 19 */
	{ "weekday day heat",   SV|OK, omcs_temp,  omcb_temp  }, /* 1A */
	{ "weekday eve time",   SV|OK, omcs_ptime, omcb_ptime }, /* 1B */
	{ "weekday eve cool",   SV|OK, omcs_temp,  omcb_temp  }, /* 1C */
	{ "weekday eve heat",   SV|OK, omcs_temp,  omcb_temp  }, /* 1D */
	{ "weekday night time", SV|OK, omcs_ptime, omcb_ptime }, /* 1E */
	{ "weekday night cool", SV|OK, omcs_temp,  omcb_temp  }, /* 1F */
	{ "weekday night heat", SV|OK, omcs_temp,  omcb_temp  }, /* 20 */

	{ "saturday morn time",  SV|OK, omcs_ptime, omcb_ptime }, /* 21 */
	{ "saturday morn cool",  SV|OK, omcs_temp,  omcb_temp  }, /* 22 */
	{ "saturday morn heat",  SV|OK, omcs_temp,  omcb_temp  }, /* 23 */
	{ "saturday day time",   SV|OK, omcs_ptime, omcb_ptime }, /* 24 */
	{ "saturday day cool",   SV|OK, omcs_temp,  omcb_temp  }, /* 25 */
	{ "saturday day heat",   SV|OK, omcs_temp,  omcb_temp  }, /* 26 */
	{ "saturday eve time",   SV|OK, omcs_ptime, omcb_ptime }, /* 27 */
	{ "saturday eve cool",   SV|OK, omcs_temp,  omcb_temp  }, /* 28 */
	{ "saturday eve heat",   SV|OK, omcs_temp,  omcb_temp  }, /* 29 */
	{ "saturday night time", SV|OK, omcs_ptime, omcb_ptime }, /* 2A */
	{ "saturday night cool", SV|OK, omcs_temp,  omcb_temp  }, /* 2B */
	{ "saturday night heat", SV|OK, omcs_temp,  omcb_temp  }, /* 2C */

	{ "sunday morn time",    SV|OK, omcs_ptime, omcb_ptime }, /* 2D */
	{ "sunday morn cool",    SV|OK, omcs_temp,  omcb_temp  }, /* 2E */
	{ "sunday morn heat",    SV|OK, omcs_temp,  omcb_temp  }, /* 2F */
	{ "sunday day time",     SV|OK, omcs_ptime, omcb_ptime }, /* 30 */
	{ "sunday day cool",     SV|OK, omcs_temp,  omcb_temp  }, /* 31 */
	{ "sunday day heat",     SV|OK, omcs_temp,  omcb_temp  }, /* 32 */
	{ "sunday eve time",     SV|OK, omcs_ptime, omcb_ptime }, /* 33 */
	{ "sunday eve cool",     SV|OK, omcs_temp,  omcb_temp  }, /* 34 */
	{ "sunday eve heat",     SV|OK, omcs_temp,  omcb_temp  }, /* 35 */
	{ "sunday night time",   SV|OK, omcs_ptime, omcb_ptime }, /* 36 */
	{ "sunday night cool",   SV|OK, omcs_temp,  omcb_temp  }, /* 37 */
	{ "sunday night heat",   SV|OK, omcs_temp,  omcb_temp  }, /* 38 */

	{ "reserved",           RESV, NULL,             NULL }, /* 39 */

	{ "day",                  OK, omcs_day,  omcb_int  }, /* 3A */
	{ "cool setpoint",   PUBA|SV|OK, omcs_temp, omcb_temp, "cool_set"}, /* 3B */
	{ "heat setpoint",   PUBA|SV|OK, omcs_temp, omcb_temp, "heat_set"}, /* 3C */
	{ "thermostat mode", PUBA|SV|OK, omcs_mode, omcb_mode, "tstatmode" }, /* 3D */
	{ "fan mode",        PUBC|SV|OK, omcs_fanm, omcb_fanm, "fanmode"}, /* 3E */
	{ "hold",            PUBC|SV|OK, omcs_hold, omcb_hold, "holdmode"  }, /* 3F */

	{ "current temp",   PUBA|ROK, omcs_temp,  NULL,     "current" }, /* 40 */
	{ "seconds",              OK, omcs_int,  omcb_int  }, /* 41 */
	{ "minutes",              OK, omcs_int,  omcb_int  }, /* 42 */
	{ "hours",                OK, omcs_int,  omcb_int  }, /* 43 */
	{ "outside temp",         OK, omcs_temp, omcb_temp }, /* 44 */
	{ "reserved",           RESV, omcs_int,   NULL     }, /* 45 */
	{ "RTP mode",          SV|OK, omcs_int,  omcb_int  }, /* 46 */
	{ "current mode",   PUBA|ROK, omcs_mode,  NULL ,   "curmode"   }, /* 47 */
	{ "output status",  PUBA|ROK, omcs_outst,   NULL,  "outstatus"   }, /* 48 */
	{ "model",          PUBA|ROK, omcs_model, NULL,   "model"    }, /* 49 */
};

const int rc8x_nregs = sizeof(rc8x_regs)/sizeof(struct omst_reg);

/*
 * Register names, read/write classification,
 * and appropriate byte <-> string conversion routines
 */
struct omst_reg rc2000_regs[] = {
	{ "address",                 		    ROK, omcs_int,  omcb_int  }, /* 0 */
	{ "comm mode",               		    ROK, omcs_int,  omcb_int  }, /* 1 */
	{ "sys options",             		    ROK, omcs_int,  omcb_int  }, /* 2 */
	{ "display options",         		  SV|OK, omcs_int,  omcb_int  }, /* 3 */
	{ "temp calibration offset" ,		  SV|OK, omcs_tcal, omcb_tcal }, /* 4 */
	{ "cool limit",         		  SV|OK, omcs_temp, omcb_temp }, /* 5 */
	{ "heat limit",         		  SV|OK, omcs_temp, omcb_temp }, /* 6 */
	{ "energy-efficient control mode enable", OKG, omcs_int,  NULL      }, /* 7 */
	{ "current omni version",                 ROK, omcs_int,  NULL      }, /* 8 */
	{ "cool anticipator",            	  SV|OK, omcs_int,  omcb_int  }, /* 09 */
	{ "second stage differential",   	  SV|OK, omcs_int,  omcb_int  }, /* 0A */
	{ "cool cycle time",    		  SV|OK, omcs_int,  omcb_int  }, /* 0B */
	{ "heat cycle time",    		  SV|OK, omcs_int,  omcb_int  }, /* 0C */
	{ "aux/3rd-stage heat diff",              SV|OK, omcs_int,  omcb_int  }, /* 0D */
	{ "clock adjust",       		   SV|OK, omcs_ccal, omcb_ccal }, /* 0E */
	{ "filter days left",   		     OK, omcs_int,  omcb_int  }, /* 0F */

	{ "hours run this week", 		    ROK, omcs_int, omcb_int }, /* 10 */
	{ "hours run last week", 		    ROK, omcs_int, omcb_int }, /* 11 */
	{ "RTP setback",         		  SV|OK, omcs_int, omcb_int }, /* 12 */
	{ "RTP high",            		  SV|OK, omcs_int, omcb_int }, /* 13 */
	{ "RTP crit",            		  SV|OK, omcs_int, omcb_int }, /* 14 */

	{ "monday morn time",  	 SV|OK, omcs_ptime, omcb_ptime }, /* 15 */
	{ "monday morn cool",  	 SV|OK, omcs_temp,  omcb_temp  }, /* 16 */
	{ "monday morn heat",  	 SV|OK, omcs_temp,  omcb_temp  }, /* 17 */
	{ "monday day time",   	 SV|OK, omcs_ptime, omcb_ptime }, /* 18 */
	{ "monday day cool",   	 SV|OK, omcs_temp,  omcb_temp  }, /* 19 */
	{ "monday day heat",   	 SV|OK, omcs_temp,  omcb_temp  }, /* 1A */
	{ "monday eve time",   	 SV|OK, omcs_ptime, omcb_ptime }, /* 1B */
	{ "monday eve cool",   	 SV|OK, omcs_temp,  omcb_temp  }, /* 1C */
	{ "monday eve heat",   	 SV|OK, omcs_temp,  omcb_temp  }, /* 1D */
	{ "monday night time", 	 SV|OK, omcs_ptime, omcb_ptime }, /* 1E */
	{ "monday night cool", 	 SV|OK, omcs_temp,  omcb_temp  }, /* 1F */
	{ "monday night heat", 	 SV|OK, omcs_temp,  omcb_temp  }, /* 20 */

	{ "saturday morn time",  SV|OK, omcs_ptime, omcb_ptime }, /* 21 */
	{ "saturday morn cool",  SV|OK, omcs_temp,  omcb_temp  }, /* 22 */
	{ "saturday morn heat",  SV|OK, omcs_temp,  omcb_temp  }, /* 23 */
	{ "saturday day time",   SV|OK, omcs_ptime, omcb_ptime }, /* 24 */
	{ "saturday day cool",   SV|OK, omcs_temp,  omcb_temp  }, /* 25 */
	{ "saturday day heat",   SV|OK, omcs_temp,  omcb_temp  }, /* 26 */
	{ "saturday eve time",   SV|OK, omcs_ptime, omcb_ptime }, /* 27 */
	{ "saturday eve cool",   SV|OK, omcs_temp,  omcb_temp  }, /* 28 */
	{ "saturday eve heat",   SV|OK, omcs_temp,  omcb_temp  }, /* 29 */
	{ "saturday night time", SV|OK, omcs_ptime, omcb_ptime }, /* 2A */
	{ "saturday night cool", SV|OK, omcs_temp,  omcb_temp  }, /* 2B */
	{ "saturday night heat", SV|OK, omcs_temp,  omcb_temp  }, /* 2C */

	{ "sunday morn time",    SV|OK, omcs_ptime, omcb_ptime }, /* 2D */
	{ "sunday morn cool",    SV|OK, omcs_temp,  omcb_temp  }, /* 2E */
	{ "sunday morn heat",    SV|OK, omcs_temp,  omcb_temp  }, /* 2F */
	{ "sunday day time",     SV|OK, omcs_ptime, omcb_ptime }, /* 30 */
	{ "sunday day cool",     SV|OK, omcs_temp,  omcb_temp  }, /* 31 */
	{ "sunday day heat",     SV|OK, omcs_temp,  omcb_temp  }, /* 32 */
	{ "sunday eve time",     SV|OK, omcs_ptime, omcb_ptime }, /* 33 */
	{ "sunday eve cool",     SV|OK, omcs_temp,  omcb_temp  }, /* 34 */
	{ "sunday eve heat",     SV|OK, omcs_temp,  omcb_temp  }, /* 35 */
	{ "sunday night time",   SV|OK, omcs_ptime, omcb_ptime }, /* 36 */
	{ "sunday night cool",   SV|OK, omcs_temp,  omcb_temp  }, /* 37 */
	{ "sunday night heat",   SV|OK, omcs_temp,  omcb_temp  }, /* 38 */

	{ "outside humidity",       OK, omcs_int,    omcb_int }, /* 39 */

	{ "day (0=sunday",          OK, omcs_day,  omcb_int  }, /* 3A */
	{ "cool setpoint",     SV|OK, omcs_temp, omcb_temp }, /* 3B */
	{ "heat setpoint",     SV|OK, omcs_temp, omcb_temp }, /* 3C */
	{ "thermostat mode",   SV|OK, omcs_mode, omcb_mode }, /* 3D */
	{ "fan mode",          SV|OK, omcs_fanm, omcb_fanm }, /* 3E */
	{ "hold",              SV|OK, omcs_hold, omcb_hold  }, /* 3F (63) (0=Off, 1=On, 2=Vacation) */

	{ "current temp",        ROK, omcs_temp,  NULL     }, /* 40 */
	{ "seconds",              OK, omcs_int,  omcb_int  }, /* 41 */
	{ "minutes",              OK, omcs_int,  omcb_int  }, /* 42 */
	{ "hours",                OK, omcs_int,  omcb_int  }, /* 43 */
	{ "outside temp",         OK, omcs_temp, omcb_temp }, /* 44 */
	{ "reserved",           RESV, omcs_int,   NULL     }, /* 45 */
	{ "RTP mode",          SV|OK, omcs_int,  omcb_int  }, /* 46 */
	{ "current mode",        ROK, omcs_mode,  NULL     }, /* 47 */
	{ "output status",       ROK, omcs_outst,   NULL     }, /* 48 */
	{ "model",               ROK, omcs_model, NULL     }, /* 0x49 (73) */
	
	{ "Current energy cost", OK, omcs_int, omcb_int }, /* 0x50 (74) */

/*          Programming Tuesday - Friday:*/

	{ "Tuesday morning time",            OKG, omcs_ptime, omcb_ptime}, /* 75  */
	{ "Tuesday morning cool setpoint",   OKG, omcs_temp,  omcb_temp }, /* 76  */
	{ "Tuesday morning heat setpoint",   OKG, omcs_temp,  omcb_temp }, /* 77  */
	{ "Tuesday day time",                OKG, omcs_ptime, omcb_ptime}, /* 78  */
	{ "Tuesday day cool setpoint",       OKG, omcs_temp,  omcb_temp }, /* 79  */
	{ "Tuesday day heat setpoint",       OKG, omcs_temp,  omcb_temp }, /* 80  */
	{ "Tuesday evening time",            OKG, omcs_ptime, omcb_ptime}, /* 81  */
	{ "Tuesday evening cool setpoint",   OKG, omcs_temp,  omcb_temp }, /* 82  */
	{ "Tuesday evening heat setpoint",   OKG, omcs_temp,  omcb_temp }, /* 83  */
	{ "Tuesday night time",              OKG, omcs_ptime, omcb_ptime}, /* 84  */
	{ "Tuesday night cool setpoint",     OKG, omcs_temp,  omcb_temp }, /* 85  */
	{ "Tuesday night heat setpoint",     OKG, omcs_temp,  omcb_temp }, /* 86  */
	{ "Wednesday morning time",          OKG, omcs_ptime, omcb_ptime}, /* 87  */
	{ "Wednesday morning cool setpoint", OKG, omcs_temp,  omcb_temp }, /* 88  */
	{ "Wednesday morning heat setpoint", OKG, omcs_temp,  omcb_temp }, /* 89  */
	{ "Wednesday day time",              OKG, omcs_ptime, omcb_ptime}, /* 90  */
	{ "Wednesday day cool setpoint",     OKG, omcs_temp,  omcb_temp }, /* 91  */
	{ "Wednesday day heat setpoint",     OKG, omcs_temp,  omcb_temp }, /* 92  */
	{ "Wednesday evening time",          OKG, omcs_ptime, omcb_ptime}, /* 93  */
	{ "Wednesday evening cool setpoint", OKG, omcs_temp,  omcb_temp }, /* 94  */
	{ "Wednesday evening heat setpoint", OKG, omcs_temp,  omcb_temp }, /* 95  */
	{ "Wednesday night time",            OKG, omcs_ptime, omcb_ptime}, /* 96  */
	{ "Wednesday night cool setpoint",   OKG, omcs_temp,  omcb_temp }, /* 97  */
	{ "Wednesday night heat setpoint",   OKG, omcs_temp,  omcb_temp }, /* 98  */
	{ "Thursday morning time",           OKG, omcs_ptime, omcb_ptime}, /* 99  */
	{ "Thursday morning cool setpoint",  OKG, omcs_temp,  omcb_temp }, /* 100 */
	{ "Thursday morning heat setpoint",  OKG, omcs_temp,  omcb_temp }, /* 101 */
	{ "Thursday day time",               OKG, omcs_ptime, omcb_ptime}, /* 102 */
	{ "Thursday day cool setpoint",      OKG, omcs_temp,  omcb_temp }, /* 103 */
	{ "Thursday day heat setpoint",      OKG, omcs_temp,  omcb_temp }, /* 104 */
	{ "Thursday evening time",           OKG, omcs_ptime, omcb_ptime}, /* 105 */
	{ "Thursday evening cool setpoint",  OKG, omcs_temp,  omcb_temp }, /* 106 */
	{ "Thursday evening heat setpoint",  OKG, omcs_temp,  omcb_temp }, /* 107 */
	{ "Thursday night time",             OKG, omcs_ptime, omcb_ptime}, /* 108 */
	{ "Thursday night cool setpoint",    OKG, omcs_temp,  omcb_temp }, /* 109 */
	{ "Thursday night heat setpoint",    OKG, omcs_temp,  omcb_temp }, /* 110 */
	{ "Friday morning time",             OKG, omcs_ptime, omcb_ptime}, /* 111 */
	{ "Friday morning cool setpoint",    OKG, omcs_temp,  omcb_temp }, /* 112 */
	{ "Friday morning heat setpoint",    OKG, omcs_temp,  omcb_temp }, /* 113 */
	{ "Friday day time",                 OKG, omcs_ptime, omcb_ptime}, /* 114 */
	{ "Friday day cool setpoint", 	     OKG, omcs_temp,  omcb_temp }, /* 115 */
	{ "Friday day heat setpoint", 	     OKG, omcs_temp,  omcb_temp }, /* 116 */
	{ "Friday evening time",             OKG, omcs_ptime, omcb_ptime}, /* 117 */
	{ "Friday evening cool setpoint",    OKG, omcs_temp,  omcb_temp }, /* 118 */
	{ "Friday evening heat setpoint",    OKG, omcs_temp,  omcb_temp }, /* 119 */
	{ "Friday night time",               OKG, omcs_ptime, omcb_ptime}, /* 120 */
	{ "Friday night cool setpoint",      OKG, omcs_temp,  omcb_temp }, /* 121 */
	{ "Friday night heat setpoint",      OKG, omcs_temp,  omcb_temp }, /* 122 */
									       
//      Programming Occupancy:						       
									       
	{ "Day Cool setpoint",               OKG, omcs_temp,  omcb_temp }, /* 123 */
	{ "Day Heat setpoint",               OKG, omcs_temp,  omcb_temp }, /* 124 */
	{ "Night Cool setpoint",             OKG, omcs_temp,  omcb_temp }, /* 125 */
	{ "Night Heat setpoint",             OKG, omcs_temp,  omcb_temp }, /* 126 */
	{ "Away Cool setpoint",              OKG, omcs_temp,  omcb_temp }, /* 127 */
	{ "Away Heat setpoint",              OKG, omcs_temp,  omcb_temp }, /* 128 */
	{ "Vacation Cool setpoint",          OKG, omcs_temp,  omcb_temp }, /* 129 */
	{ "Vacation Heat setpoint",          OKG, omcs_temp,  omcb_temp },  /* 130 */

//      Setup:								       
									       
	{ "Program mode (0=None, 1=Schedule, 2=Occupancy)",            OK, omcs_int, omcb_int}, /* 131 */
	{ "Expansion baud (0=300, 1=100, 42=1200, 54=2400, 126=9600)", ROK, omcs_int, omcb_int}, /* 132 */
	{ "Days until filter reminder appears",                        OK, omcs_int, omcb_int}, /* 133 */
	{ "Humidity Setpoint",                                         OKG, omcs_int, omcb_int}, /* 134 */
	{ "Dehumidify Setpoint",                                       OKG, omcs_int, omcb_int}, /* 135 */
	{ "Dehumidifier output options (0=Not used, 1=Standalone, 2= variable speed fan)", OK, omcs_int, omcb_int}, /* 136*/
	{ "Humidifier output (0=Not used, 1=Standalone)",              OK, omcs_int, omcb_int}, /* 137 */
	{ "Minutes out of 20 that fan is on during cycle (1-19)",      OKG, omcs_int, omcb_int}, /* 138 */
	{ "Backlight settings (0=Off, 1=On, 2=Auto)",                  OK, omcs_int, omcb_int}, /* 139 */
	{ "Backlight color (0-100)",                                   OK, omcs_int, omcb_int}, /* 140 */
	{ "Backlight intensity (1-10)",                                OK, omcs_int, omcb_int}, /* 141 */
	{ "Selective message enable/disable",                          ROK, omcs_int, omcb_int}, /* 142 */
	{ "Minimum on time for cool (2-30)",  			       OKG, omcs_int, omcb_int}, /* 143 */
	{ "Minimum off time for cool (2-30)", 			       OKG, omcs_int, omcb_int}, /* 144 */
	{ "Minimum on time for heat (2-30)",  			       OKG, omcs_int, omcb_int}, /* 145 */
	{ "Minimum off time for heat (2-30)", 			       OKG, omcs_int, omcb_int}, /* 146 */
	{ "System type (0=Heat Pump, 1=Conventional, 2=Dual Fuel)",    OKG, omcs_int, omcb_int}, /* 147 */
	{ "Reserved",                                                 RESV, omcs_int, omcb_int}, /* 148 */
	{ "End of vacation date: day",                                  OK, omcs_int, omcb_int}, /* 149 */
	{ "unknown/reserved",                                  RESV, omcs_int, omcb_int}, /* 150 */
	{ "End of vacation date: hour",                                 OK, omcs_int, omcb_int}, /* 151 */
	{ "Hours HVAC used in Week 0", 					ROK, omcs_int, omcb_int}, /* 152 */
	{ "Hours HVAC used in Week 1", 					ROK, omcs_int, omcb_int}, /* 153 */
	{ "Hours HVAC used in Week 2", 					ROK, omcs_int, omcb_int}, /* 154 */
	{ "Hours HVAC used in Week 3", 					ROK, omcs_int, omcb_int}, /* 155 */
	{ "Reserved", 			   				RESV, omcs_int, omcb_int}, /* 156 */
	{ "Reserved", 			   				RESV, omcs_int, omcb_int}, /* 157 */
	{ "Enable/disable individual temp sensors",                     OK, omcs_int, omcb_int}, /* 158 */
	{ "Number of cool stages", 	   				OK, omcs_int, omcb_int}, /* 159 */
	{ "Number of heat stages", 	   				OK, omcs_int, omcb_int}, /* 160 */
	{ "Current occupancy mode (0=Day, 1=Night, 2=Away, 3=Vacation)", ROK, omcs_int, omcb_int}, /* 161 */
	{ "Current indoor humidity",                 			ROK, omcs_int, omcb_int}, /* 162 */
	{ "Cool setpoint for vacation mode (51-91)", 			OKG, omcs_int, omcb_int}, /* 163 */
	{ "Heat setpoint for vacation mode (51-91)", 			OKG, omcs_int, omcb_int}, /* 164 */
									       
	//      Energy: not fully documented, so not enabled for write.
									       
	{ "Displayed price of energy with medium level energy",   ROK, omcs_int, omcb_int}, /* 165 */
	{ "Displayed price of energy with high level energy",     ROK, omcs_int, omcb_int}, /* 166 */
	{ "Displayed price of energy with critical level energy", ROK, omcs_int, omcb_int}, /* 167 */
	{ "Sensitivity setting for proximity sensor (0-99)",      ROK, omcs_int, omcb_int}, /* 168 */
	{ "Energy level as set by the meter",                     ROK, omcs_int, omcb_int}, /* 169 */
	{ "Current energy total cost, upper byte",                ROK, omcs_int, omcb_int}, /* 170 */
	{ "Current energy total cost, lower byte",                ROK, omcs_int, omcb_int}, /* 171 */

#ifdef REGS2000_HACK
	
	{ "STRING ASCII display for first load control module",   RESV, omcs_int, omcb_int}, /* 172 */
	{ "STRING ASCII display for second load control module",  RESV, omcs_int, omcb_int}, /* 173 */
	{ "STRING ASCII display for third load control module",   RESV, omcs_int, omcb_int}, /* 174 */
	{ "STRING ASCII display for Energy message",              RESV, omcs_int, omcb_int}, /* 175 */
	{ "STRING ASCII display for emergency broadcast message (not implemented)", RESV, omcs_int, omcb_int}, /* 176 */
	{ "STRING ASCII display for custom message (not implemented)", RESV, omcs_int, omcb_int}, /* 177 */
	{ "STRING ASCII display for energy graph title bar",           RESV, omcs_int, omcb_int}, /* 178 */
	{ "STRING ASCII display for energy graph x axis", 	       RESV, omcs_int, omcb_int}, /* 179 */
	{ "STRING ASCII display for energy graph y axis", 	       RESV, omcs_int, omcb_int}, /* 180 */
	{ "STRING ASCII display for long messages (not implemented)",  RESV, omcs_int, omcb_int}, /* 181 */
	{ "graph bar max height, upper byte",		               RESV, omcs_int, omcb_int}, /* 182 */
	{ "graph bar max height, lower byte",			       RESV, omcs_int, omcb_int}, /* 183 */
	{ "graph bar one value, upper byte", 			       RESV, omcs_int, omcb_int}, /* 184 */
	{ "graph bar one value, lower byte", 			       RESV, omcs_int, omcb_int}, /* 185 */
	{ "graph bar two value, upper byte", 			       RESV, omcs_int, omcb_int}, /* 186 */
	{ "graph bar two value, lower byte", 			       RESV, omcs_int, omcb_int}, /* 187 */
	{ "graph bar three value, upper byte",			       RESV, omcs_int, omcb_int}, /* 188 */
	{ "graph bar three value, lower byte",			       RESV, omcs_int, omcb_int}, /* 189 */
	{ "graph bar four value, upper byte", 			       RESV, omcs_int, omcb_int}, /* 190 */
	{ "graph bar four value, lower byte", 			       RESV, omcs_int, omcb_int}, /* 191 */
	{ "Status and enable/disable of each load control module",     RESV, omcs_int, omcb_int}, /* 192 */

	{ "unused", RESV, omcs_int, omcb_int}, /* 193 */
	{ "unused", RESV, omcs_int, omcb_int}, /* 194 */
	{ "unused", RESV, omcs_int, omcb_int}, /* 195 */
	{ "unused", RESV, omcs_int, omcb_int}, /* 196 */
	{ "unused", RESV, omcs_int, omcb_int}, /* 197 */
	{ "unused", RESV, omcs_int, omcb_int}, /* 198 */
	{ "unused", RESV, omcs_int, omcb_int}, /* 199 */
	
//      Sensors:								       
									       
	{ "Current temperature of sensor 3", 			       ROK, omcs_int, omcb_int}, /* 200 */
	{ "Current temperature of sensor 4", 			       ROK, omcs_int, omcb_int}, /* 201 */
	{ "Reserved",                        			       RESV, omcs_int, omcb_int}, /* 202 */
	
	{ "unused", RESV, omcs_int, omcb_int}, /* 203 */
	{ "unused", RESV, omcs_int, omcb_int}, /* 204 */
	{ "unused", RESV, omcs_int, omcb_int}, /* 205 */
	{ "unused", RESV, omcs_int, omcb_int}, /* 206 */
	{ "unused", RESV, omcs_int, omcb_int}, /* 207 */
	{ "unused", RESV, omcs_int, omcb_int}, /* 208 */
	{ "unused", RESV, omcs_int, omcb_int}, /* 209 */
	{ "unused", RESV, omcs_int, omcb_int}, /* 210 */
	{ "unused", RESV, omcs_int, omcb_int}, /* 211 */
	{ "unused", RESV, omcs_int, omcb_int}, /* 212 */
	{ "unused", RESV, omcs_int, omcb_int}, /* 213 */
	{ "unused", RESV, omcs_int, omcb_int}, /* 214 */
	{ "unused", RESV, omcs_int, omcb_int}, /* 215 */
	{ "unused", RESV, omcs_int, omcb_int}, /* 216 */
	{ "unused", RESV, omcs_int, omcb_int}, /* 217 */
	{ "unused", RESV, omcs_int, omcb_int}, /* 218 */
	{ "unused", RESV, omcs_int, omcb_int}, /* 219 */
	{ "unused", RESV, omcs_int, omcb_int}, /* 220 */
	{ "unused", RESV, omcs_int, omcb_int}, /* 221 */
	{ "unused", RESV, omcs_int, omcb_int}, /* 222 */
	{ "unused", RESV, omcs_int, omcb_int}, /* 223 */


//	Wireless: not mentioned in manual, so not emabled for write
									       
	{ "Wireless MAC address byte 1", ROK, omcs_int, omcb_int}, /* 224 */
	{ "Wireless MAC address byte 2", ROK, omcs_int, omcb_int}, /* 225 */
	{ "Wireless MAC address byte 3", ROK, omcs_int, omcb_int}, /* 226 */
	{ "Wireless MAC address byte 4", ROK, omcs_int, omcb_int}, /* 227 */
	{ "Wireless MAC address byte 5", ROK, omcs_int, omcb_int}, /* 228 */
	{ "Wireless MAC address byte 6", ROK, omcs_int, omcb_int}, /* 229 */
	{ "Wireless MAC address byte 7", ROK, omcs_int, omcb_int}, /* 230 */
	{ "Wireless MAC address byte 8", ROK, omcs_int, omcb_int}, /* 231 */
	{ "Wireless firmware version integer place", ROK, omcs_int, omcb_int}, /* 232 */
	{ "Wireless firmware version decimal place", ROK, omcs_int, omcb_int}, /* 233 */
	{ "Wireless strength (0-100)", ROK, omcs_int, omcb_int}, /* 234 */
	{ "Wireless buzzer enable or disable", ROK, omcs_int, omcb_int}, /* 235 */
	{ "Wireless IP address byte 1", ROK, omcs_int, omcb_int}, /* 236 */
	{ "Wireless IP address byte 2", ROK, omcs_int, omcb_int}, /* 237 */
	{ "Wireless IP address byte 3", ROK, omcs_int, omcb_int}, /* 238 */
	{ "Wireless IP address byte 4", ROK, omcs_int, omcb_int}, /* 239 */
	{ "Reserved", RESV, omcs_int, omcb_int}, /* 253 */
	{ "Reserved", RESV, omcs_int, omcb_int}, /* 254 */

#endif
	
};

const int rc2000_nregs = sizeof(rc2000_regs)/sizeof(struct omst_reg);




/*
 * conversion routines
 *	omcb_* - convert string to byte ready to send.
 *	omcs_* - convert byte read from omnistat into string.
 *
 */
unsigned char omcb_int(char *s) 
{
	int i;
	i = atoi(s);
	
	return i & 0xff;
}
void omcs_int(char *sp, unsigned char b)
{
	sprintf(sp, "%d", b);
}

unsigned char omcb_temp(char *s)
{
	unsigned char b;
	double td;
	td = strtod(s, NULL);

	if(!omst_celsius)
		td = (td + 40) * (5.0/9.0) - 40;
	b = ((td + 40.0) * 2.0) + 0.5;
	return b;
}
double omcf_temp(unsigned char b, int celsius)  // temperature byte to float
{
	double ct, ft;
       
	ct = (double)b / 2 - 40;
	if(celsius)
		return ct;
	else {
		ft = (ct + 40) * 1.8 - 40;
		return ft;
	}
}
void omcs_temp(char *sp, unsigned char b)  // temperature byte to string
{
	double ct, ft;
       
	ct = (double)b / 2 - 40;
	
	if(omst_celsius)
		sprintf(sp, "%.1f", ct);
	else {
		ft = (ct + 40) * 1.8 - 40;
		sprintf(sp, "%.f", ft);
	}
}

/* program time for automatic morning/day/evening/night setting changes */
unsigned char omcb_ptime(char *s)
{
	unsigned char b;
	int hour, min;
	if(strcmp(s, "none") == 0) {
		b = 96;
	} else {
		sscanf(s, "%d:%d", &hour, &min);
		b = hour*4 + min/15;
	}
	return b;
}
void omcs_ptime(char *sp, unsigned char b)
{
	int hour, min;

	if(b >= 96)
		strcpy(sp, "none");
	else {
		hour = b / 4;
		min = (b % 4) * 15;
		sprintf(sp, "%02d:%02d", hour, min);
	}		
}

/* temperature calibration - always 1/2 degree C increments */
unsigned char omcb_tcal(char *s)
{
	double td;
	int i;
	td = atof(s);

	i = td * 2 + 30;
	if(i < 1)
		i = 1;
	if(i > 59)
		i = 59;

	return (unsigned char)i;
}
void omcs_tcal(char *sp, unsigned char b)
{
	sprintf(sp, "%+.f", ((double)b - 30)/2);
}

/* clock calibration, signed seconds/day */
unsigned char omcb_ccal(char *s)
{
	int i;
	i = atoi(s) + 30;
	if(i < 1)
		i = 1;
	if(i > 59)
		i = 59;

	return (unsigned char)(i & 0xff);
}
void omcs_ccal(char *sp, unsigned char b)
{
	sprintf(sp, "%+d", b - 30);
}


/* omnistat model - output only */
void omcs_model(char *sp, unsigned char b)
{
	switch(b) {
	case 0:
		strcpy(sp, "RC-80");
		break;
	case 1:
		strcpy(sp, "RC-81");
		break;
	case 8:
		strcpy(sp, "RC-90");
		break;
	case 9:
		strcpy(sp, "RC-91");
		break;
	case 16:
		strcpy(sp, "RC-100");
		break;
	case 17:
		strcpy(sp, "RC-101");
		break;
	case 34:
		strcpy(sp, "RC-112");
		break;
	case 48:
		strcpy(sp, "RC-120");
		break;
	case 49:
		strcpy(sp, "RC-121");
		break;
	case 50:
		strcpy(sp, "RC-122");
		break;
	case 0x78:
		strcpy(sp, "RC-2000");
		break;
	default:
		sprintf(sp, "rc%d?", b);
		break;
	}
}

struct omst_reg *
om_model_table(unsigned char model)
{
	switch(model) {
	case 0:
	case 1:
	case 8:
	case 9:
	case 16:
	case 17:
	case 34:
	case 48:
	case 49:
	case 50:
		return rc8x_regs;
	case 0x78:
		return rc2000_regs;
	default:
		return NULL;
	}
}

int om_model_table_size(unsigned char model)
{
	switch(model) {
	case 0:
	case 1:
	case 8:
	case 9:
	case 16:
	case 17:
	case 34:
	case 48:
	case 49:
	case 50:
		return rc8x_nregs;
	case 0x78:
		return rc2000_nregs;
	default:
		return 0;
	}
}



/* operating and thermostat modes */
static char *mode_strings[] = {"off", "heat", "cool", "auto", "em-heat"};
static const int n_mode_strings = sizeof(mode_strings)/sizeof(char *);

unsigned char omcb_mode(char *s)
{
	unsigned char b;
	int i;
	for(i = 0; i < n_mode_strings; i++) {
		if(0==strcasecmp(s, mode_strings[i]))
			return (unsigned char)i;
	}
	return 0;
}

void omcs_mode(char *sp, unsigned char b)
{
	if(b < n_mode_strings)
		strcpy(sp, mode_strings[b]);
	else
		sprintf(sp, "mode %d?", b);
}

/* day of week */ 
static char *daynames[] = {"Monday", "Tuesay", "Wednesday", "Thursday",
			   "Friday", "Saturday", "Sunday"};
void
omcs_day(char *sp, unsigned char b) 
{ 
	if(b < 7)
		strcpy(sp, daynames[b]); 
	else
		sprintf(sp, "day %d?", b);
}

/* fan mode */ 
static char *fan_modes[] = {"auto", "on"};
unsigned char omcb_fanm(char *s)
{
	unsigned char b;
	int i;
	for(i = 0; i < 2; i++) {
		if(0==strcasecmp(s, fan_modes[i]))
			return (unsigned char)i;
	}
	return 0;
}
void
omcs_fanm(char *sp, unsigned char b) 
{ 
	if(b < 2)
		strcpy(sp, fan_modes[b]); 
	else
		sprintf(sp, "fan mode %d?", b);
}

/* hold mode */ 
static char *hold_modes[] = {"auto", "hold"};
unsigned char omcb_hold(char *s)
{
	unsigned char b;
	if(strcmp(s, "auto") == 0)
		return 0;
	else if(strcmp(s, "hold") == 0)
		return 255;
	else
		return 0;
}
void
omcs_hold(char *sp, unsigned char b) 
{ 
	if(b == 0)
		strcpy(sp, "auto");
	else if(b == 255)
		strcpy(sp, "hold");
	else
		sprintf(sp, "hold mode %d?", b);
}

/* output bit status */
void
omcs_outst(char *sp, unsigned char b) 
{ 
	if(b & 1)
		strcpy(sp, "heat");
	else
		strcpy(sp, "cool");

	if(b & 2)
		strcat(sp, "|em-heat");
	if(b & 4)
		strcat(sp, "|run");
	if(b & 8)
		strcat(sp, "|fan");
	if(b & 16)
		strcat(sp, "|s2");
}


/*
 * given a register address, and a value to/from that register, convert the value to a
 * string, assuming the indicated omnistat model.
 */
void omcs_regval(char *str, unsigned char regaddr, unsigned char val, unsigned char model)
{
	struct omst_reg *regtab = om_model_table(model);
	int max_regs = om_model_table_size(model);

	if(regtab && regaddr <= max_regs) {
		regtab[regaddr].cvt_str(str, val);
	} else {
		sprintf(str, "rc%02x:REG0x%02x_0x%02x", model, regaddr, val);
	}
}

