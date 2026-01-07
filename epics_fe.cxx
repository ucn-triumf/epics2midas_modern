/********************************************************************\

  Name:         epics_fe.c
  Created by:   Stefan Ritt
  Heavily modified by Thomas Lindner                                                                                                   
  Contents:     General EPICS frontend for TRIUMF UCN experiment

\********************************************************************/

#include <cstring>

#include <epicsStdlib.h>
#include "cadef.h"
#include "dbDefs.h"
#include "epicsTime.h"

#include "midas.h"
#include "mfe.h"
#include "odbxx.h"

/*-- Globals -------------------------------------------------------*/

// timeout in seconds for caget operations
#define CAGET_TIMEOUT 30.0

/* The frontend name (client name) as seen by other MIDAS clients   */
const char *frontend_name = "EPICS Frontend";
/* The frontend file name, don't change it */
const char *frontend_file_name = __FILE__;

/*-- Equipment list ------------------------------------------------*/

BOOL equipment_common_overwrite = TRUE;

int epics_read(char *pevent, int off);
int epics_loop(void);
int epics_init(void);
int epics_get_measured(int channel);


EQUIPMENT equipment[] = {

   {"EPICS",                    /* equipment name */
    {21, 0,                     /* event ID, trigger mask */
     "SYSTEM",                  /* event buffer */
     EQ_PERIODIC,               /* equipment type */
     0,                         /* event source */
     "MIDAS",                   /* format */
     TRUE,                      /* enabled */
     RO_ALWAYS,
     2000,                     /* read event every 2 sec */
     0,                         /* readout pause */
     0,                         /* number of sub events */
     10,                        /* log history every 10 seconds */
     "", "", ""} ,
    epics_read                  /* readout routine */
    },

   {""}
};

struct {
    int          length = 0;
    midas::odb   settings;
    midas::odb   variables;
    chid         *demand = nullptr;
    chid         *measured = nullptr;
    chid         *command = nullptr;
    float        *demandCache = nullptr;
    unsigned int updateInterval;
} beamline;

/*-- Error dispatcher causing communiction alarm -------------------*/

void epics_fe_error(const char *error)
{
   cm_msg(MERROR, "epics_fe_error", "%s", error);
}

/*-- Readout routine -----------------------------------------------*/

int epics_read(char *pevent, int off)
{

   float *pdata;

   // init bank structure
   bk_init(pevent);

   // create a bank with measured values
   bk_create(pevent, "E000", TID_FLOAT, (void **)&pdata);
   for (int i = 0; i < beamline.length; i++)
      *pdata++ = beamline.variables["Measured"][i];
   bk_close(pevent, pdata);

   return bk_size(pevent);
}

/*-- Frontend Init -------------------------------------------------*/

int frontend_init()
{
   /* set error dispatcher for alarm functionality */
   mfe_set_error(epics_fe_error);


   // Default values for settings
   midas::odb settings = {
     {"Update interval", 10},
     {"Names", std::array<std::string, 5>{}},
     {"CA Name", std::array<std::string, 5>{}},
     {"Enabled", std::array<bool, 5>{}},
   };
   
   // load EPICS settings from ODB
   settings.connect("/Equipment/EPICS/Settings");
   beamline.length = settings["Names"].size();
   beamline.variables.connect("/Equipment/EPICS/Variables");

   beamline.updateInterval = settings["Update interval"];

   // Should we resize all the settings parts too?
   if (!midas::odb::exists("/Equipment/EPICS/Variables/Measured"))
      beamline.variables["Measured"] = std::vector<float>(beamline.length);
   else
      beamline.variables["Measured"].resize(beamline.length);

   std::string startCommand(__FILE__);
   std::string s("build/epics_fe");
   auto i = startCommand.find("epics_fe.cxx");
   startCommand.replace(i, s.length(), s);
   startCommand = startCommand.substr(0, i + s.length());

   // set start command in ODB if not already set
   if (!midas::odb::exists("/Programs/EPICS Frontend/Start command")) {
      midas::odb efe("/Programs/EPICS Frontend");
      efe["Start command"].set_string_size(startCommand, 256);
   } else {
      midas::odb efe("/Programs/EPICS Frontend");
      if (efe["Start command"] == std::string(""))
         efe["Start command"].set_string_size(startCommand, 256);
   }

   
   install_frontend_loop(epics_loop);

   return epics_init();
}

/*------------------------------------------------------------------*/

int epics_init(void)
{

  midas::odb settings;
  settings.connect("/Equipment/EPICS/Settings");
  
   int status = FE_SUCCESS;

   /* initialize driver */
   status = ca_task_initialize();
   if (!(status & CA_M_SUCCESS)) {
      cm_msg(MERROR, "epics_init", "Unable to initialize EPICS");
      return FE_ERR_HW;
   }else{
     printf("INitialized EPICS driver\n");
   }

   beamline.measured = (chid *) calloc(beamline.length, sizeof(chid));

   for (int i = 0; i < beamline.length; i++) {
      if (!settings["Enabled"][i]) {
         printf("Channel %d disabled\r", i);
         fflush(stdout);
         continue;
      }
      printf("Channel %d\n", i);
      fflush(stdout);

      std::string name = settings["CA Name"][i];
      if (name != std::string("")) {
	//         std::string name = beamline.settings["CA Name"][i];
	//name += measured;

         status = ca_create_channel(name.c_str(), nullptr, nullptr, 0, &(beamline.measured[i]));
         SEVCHK(status, "ca_create_channel");
         if (ca_pend_io(5.0) == ECA_TIMEOUT) {
            cm_msg(MERROR, "epics_init", "Cannot connect to EPICS channel %s", name.c_str());
            status = FE_ERR_HW;
            break;
         }
      }

   }



   printf("finished epics initialize\n");

   return status;
}



/*------------------------------------------------------------------*/

int epics_get_measured(int channel)
{
   int status;
   char str[80];

   midas::odb settings;
   settings.connect("/Equipment/EPICS/Settings");

   // Skip write-only channels
   if (beamline.measured[channel] == nullptr)
      return FE_SUCCESS;

   // Skip disabled channels
   if (!settings["Enabled"][channel])
      return FE_SUCCESS;

   std::string name = settings["Names"][channel];
   if(1){

      float f;
      status = ca_get(DBR_FLOAT, beamline.measured[channel], &f);
      SEVCHK(status, "ca_get");
      if (ca_pend_io(CAGET_TIMEOUT) == ECA_TIMEOUT) {
         cm_msg(MERROR, "epics_get_measured", "Timeout on EPICS channel %s",
                name.c_str());
         return FE_ERR_HW;
      }
      beamline.variables["Measured"][channel] = f;
      if(channel == 0){printf("Measured value (0): %f\n",f);}
   }

   return FE_SUCCESS;
}

/*------------------------------------------------------------------*/


/*------------------------------------------------------------------*/

int epics_loop(void)
{
   static DWORD last_time_measured = 0;
   static DWORD last_time_demand = 0;

   // read values once per second
   if (ss_millitime() - last_time_measured > beamline.updateInterval) {
      for (int i = 0; i < beamline.length; i++)
         epics_get_measured(i);
      last_time_measured = ss_millitime();
   }
   printf("Read epics measured\n");



   ss_sleep(500); // don't eat all CPU
   return CM_SUCCESS;
}
