#include <iostream>
#include <iomanip>
#include "ecrt.h"
#include "TmtEcStructs.h"
#include "temp.h"
#include <string>
#include <queue>
#include <vector>
#include "tinystr.h"
#include "tinyxml.h"
#include "PdoEntry.h"
#include "Pdo.h"
#include "SyncManager.h"
#include "SlaveConfig.h"
#include "PdoEntryCache.h"
#include "CommandQueue.h"
#include "CyclicMotor.h"
#include "ConfigLoader.h"
#include "EtherCatServer.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <thread>


using namespace std;


/****************************************************************************/

// Application parameters

#define PRIORITY 1

/****************************************************************************/

// offsets for PDO entries
// TODO: this may be in the include file as well, not sure....
static unsigned int off_dig_out[4];
static unsigned int bp_dig_out[4];

// process data
static uint8_t *domain1_pd = NULL;


// EtherCAT
static ec_master_t *master = NULL;
static ec_master_state_t master_state = {};

static ec_domain_t *domain1 = NULL;
static ec_domain_state_t domain1_state = {};

static ec_slave_config_t *sc_ana_in = NULL;
static ec_slave_config_state_t sc_ana_in_state = {};

// Timer
static unsigned int sig_alarms = 0;
static unsigned int user_alarms = 0;

static vector<SlaveConfig> slaves;

/****************************************************************************/



// TODO: this could be in an include file designating the item
#define DigOutSlavePos 1111, 0

// TODO: this could be in an include file designating all devices
#define Beckhoff_EK1100 0x00000002, 0x044c2c52
#define Beckhoff_EL2202 0x00000002, 0x089a3052

/* Master 0, Slave 1, "EL2202"
 * Vendor ID:       0x00000002
 * Product code:    0x089a3052
 * Revision number: 0x00100000
 **/

ec_pdo_entry_info_t slave_1_pdo_entries[] = {
    {0x7000, 0x01, 1}, /* Output */
    {0x7000, 0x02, 1}, /* TriState */
    {0x7010, 0x01, 1}, /* Output */
    {0x7010, 0x02, 1}, /* TriState */
};

ec_pdo_info_t slave_1_pdos[] = {
    {0x1600, 2, slave_1_pdo_entries + 0}, /* Channel 1 */
    {0x1601, 2, slave_1_pdo_entries + 2}, /* Channel 2 */
};

ec_sync_info_t slave_1_syncs[] = {
    {0, EC_DIR_OUTPUT, 2, slave_1_pdos + 0, EC_WD_ENABLE},
    {0xff},
};

/*****************************************************************************/

tmt_ec_slave_t tmt_ec_slave[] = {

{NULL,
0x00000002,
0x089a3052,
1111,
0,
off_dig_out,
bp_dig_out,
slave_1_syncs,
slave_1_pdos,
slave_1_pdo_entries,
{"Channel 1 Output", "Channel 1 TriState", "Channel 2 Output", "Channel 2 TriState"},
4,
NULL, NULL, NULL,
},
};

/*****************************************************************************/



EtherCatServer::EtherCatServer() {
  pdoEntryCache = PdoEntryCache();
  configLoader = ConfigLoader();
};

void EtherCatServer::startServer() {

#if ENABLE_NEW_RUN_CODE

	cyclicMotor = CyclicMotor(master,  domain1, domain1_pd, slaves);

#else
	cyclicMotor = CyclicMotor(master,  domain1, off_dig_out, bp_dig_out, domain1_pd);
#endif

	cyclicMotor.start();
};

void EtherCatServer::stopServer() {
	cyclicMotor.stop();
};

int EtherCatServer::configServer(string configFile) {


  // 1. Configure the system

  master = ecrt_request_master(0);
   if (!master)
       return -1;


   // load up the configuration

   slaves = configLoader.loadConfiguration(configFile);
   cout << "\nIN EtherCatServer::configServer() 1: " << slaves.at(4).pdoEntries.at(3).domainBitPos;

   domain1 = ecrt_master_create_domain(master);
   if (!domain1)
       return -1;

  // load and apply configurations

#if ENABLE_NEW_CONFIG_CODE
  configLoader.loadConfiguration(master, domain1, &slaves);
#else
  configLoader.loadConfiguration(master, tmt_ec_slave, domain1, off_dig_out, bp_dig_out);
#endif



  printf("Activating master...\n");
  if (ecrt_master_activate(master))
      return -1;

  if (!(domain1_pd = ecrt_domain_data(domain1))) {
      return -1;
  }

  return 0;

};

vector<string> EtherCatServer::getDeviceNames() {
  vector<string> test;
  test.push_back("one");
  test.push_back("two");
  return test;
};


vector<string> EtherCatServer::getParameterNames(string deviceName) {
 
  vector<string> test;
  test.push_back(deviceName + "::one");
  test.push_back(deviceName + "::two");
  return test;
};

// TODO: should this be templated (we need to learn about templating) to 
// instead return 'int', 'float', etc??
string EtherCatServer::getParameterValue(string deviceName, string parameterName) {
 
  return deviceName + "::" + parameterName + "::" + "value";
};

// TODO: we need to template this for different types of value
void EtherCatServer::setParameterValue(string deviceName, string parameterName, int value) {
	// TEST ONLY - just use the first device

#if ENABLE_NEW_RUN_CODE

	if (parameterName.compare("Channel 1::Output") == 0) {
		PdoEntryValue pdoEntryValue = PdoEntryValue();
		pdoEntryValue.pdoEntryIndex = 0;
		pdoEntryValue.entryValue = value;
		CommandQueue::instance()->addToQueue(pdoEntryValue);
		cout << "\nChannel 1::Output added to queue";
	}

	if (parameterName.compare("Channel 1::TriState") == 0) {
		PdoEntryValue pdoEntryValue = PdoEntryValue();
		pdoEntryValue.pdoEntryIndex = 1;
		pdoEntryValue.entryValue = value;
		CommandQueue::instance()->addToQueue(pdoEntryValue);
		cout << "\nChannel 1::TriState added to queue";
	}

	if (parameterName.compare("Channel 2::Output") == 0) {
		PdoEntryValue pdoEntryValue = PdoEntryValue();
		pdoEntryValue.pdoEntryIndex = 2;
		pdoEntryValue.entryValue = value;
		CommandQueue::instance()->addToQueue(pdoEntryValue);
		cout << "\nChannel 2::Output added to queue";

	}

	if (parameterName.compare("Channel 2::TriState") == 0) {
		PdoEntryValue pdoEntryValue = PdoEntryValue();
		pdoEntryValue.pdoEntryIndex = 3;
		pdoEntryValue.entryValue = value;
		CommandQueue::instance()->addToQueue(pdoEntryValue);
		cout << "\nChannel 2::TriState added to queue";

	}

#else

	if (parameterName.compare("parameter1") == 0) {
		PdoEntryValue pdoEntryValue1 = PdoEntryValue();
		pdoEntryValue1.pdoEntryIndex = 0;
		pdoEntryValue1.entryValue = value;
		CommandQueue::instance()->addToQueue(pdoEntryValue1);
		cout << "\nparameter1 added to queue";
	}

	if (parameterName.compare("parameter2") == 0) {
		PdoEntryValue pdoEntryValue = PdoEntryValue();
		pdoEntryValue.pdoEntryIndex = 2;
		pdoEntryValue.entryValue = value;
		CommandQueue::instance()->addToQueue(pdoEntryValue);
		cout << "\nparameter2 added to queue";
	}

#endif



};



