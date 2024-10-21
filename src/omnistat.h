/*
 * omnistat wire-protocol info
 */


#ifndef OMNISTAT_H
#define OMNISTAT_H

#define FLAG_BYTE       0xAC
#define BCAST_ADDR      0x00
#define MAX_ADDR        0x7f

// max length of packet: 16 bytes
//   1 command byte minimum
//   0..15 data body bytes
#define OMNS_PKT_MAX	18

// conversion routines
extern unsigned char omst_to_reg(int r, char *aval);
extern void omst_to_string(int r, char *buf, unsigned char b);
//extern double omcf_temp(unsigned char b, int celsius);
extern double omcf_temp(unsigned char b, int celsius);  // temperature byte to float

	
/* message types (commands) that can be sent to the thermostat
 */ 
#define OMMT_GETREG     0
#define OMMT_SETREG     1
#define OMMT_GETG       2
#define OMMT_GETG2      3

/* Message status (reply tyhpes) that can recieved from the thermostat */
#define OMMS_ACK        0
#define OMMS_NACK       1
#define OMMS_DATA       2
#define OMMS_GRP1       3
#define OMMS_GRP2       4

/* register 0x47 - thermostat mode */
#define OMR_MODE_OFF    0
#define OMR_MODE_HEAT   1
#define OMR_MODE_COOL   2

/* Output status bits in register 0x48 - what the HVAC system is
   doing right now */

#define OMR_ST_HEAT     0x01
#define OMR_ST_AUXHEAT  0x02
#define OMR_ST_RUN1     0x04
#define OMR_ST_FAN      0x08
#define OMR_ST_RUN2     0x10

/*
 * conversion routines for use with register values
 */
extern unsigned char omcb_int(char *);
extern void omcs_int(char *, unsigned char);
extern unsigned char omcb_temp(char *);
extern void omcs_temp(char *, unsigned char);
extern unsigned char omcb_ptime(char *);
extern void omcs_ptime(char *, unsigned char);
extern unsigned char omcb_tcal(char *);
extern void omcs_tcal(char *, unsigned char);
extern unsigned char omcb_ccal(char *);
extern void omcs_ccal(char *, unsigned char);
extern unsigned char omcb_model(char *);
extern void omcs_model(char *, unsigned char);
extern unsigned char omcb_mode(char *);
extern void omcs_mode(char *, unsigned char);
extern unsigned char omcb_hold(char *);
extern void omcs_hold(char *, unsigned char);
extern unsigned char omcb_fanm(char *);
extern void omcs_fanm(char *, unsigned char);
extern void omcs_day(char *, unsigned char);
extern void omcs_outst(char *, unsigned char);


typedef void (*PFCS)(char *, unsigned char);
typedef unsigned char (*PFCB)(char *);

/* bit-mapped flags */
enum omreg_flags {RESV=0,       /* reserved register */
                  ROK = 1,      /* OK to read */
                  WOK = 2,      /* OK to write */
                  OK=3,         /* ROK|WOK */
                  SV=4,         /* OK to save/restore as group */
                  OKG=3|4       /* SV|OK */
                  
};      

struct omst_reg {
        char *name;
        enum omreg_flags flags; 
        PFCS cvt_str;   /* routine to convert register byte to string */
        PFCB cvt_byte;  /* routine to convert string to register byte */
};

extern struct omst_reg rc8x_regs[];
extern struct omst_reg rc2000_regs[];
extern const int rc8x_nregs;
extern const int rc2000_nregs;

extern struct omst_reg *omniregs;
extern int omst_nregs;
extern int omst_celsius;

// some omnistat register addresses known to our code

#define OM_REGADDR_MODEL	0x49
#define OM_REGADDR_COOL_SETPT	0x3B

#define OM_REGADDR_STATUS	0x3B
#define OM_REGADDR_STATUS_LEN	14

//#define OM_REGADDR_MODE		0x3f
#define OM_REGADDR_CURRENT_TEMP	0x40
#define OM_REGADDR_OUTPUT_STATE 0x48

#endif /* OMNISTAT_H */
