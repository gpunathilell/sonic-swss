#ifndef __MONTXORCH_H
#define __MONTXORCH_H

//Header Files
#include "orch.h"
#include "producerstatetable.h"
#include "observer.h"
#include "portsorch.h"
#include "selectabletimer.h"
#include "table.h"

#include "port.h"
#include "logger.h"
#include "sai_serialize.h"
//#include "swssnet.h"
#include "converter.h"
#include "portsorch.h"
#include "select.h"
#include "timer.h"

#include <vector>
#include <linux/if_ether.h>

#include <unordered_map>
#include <utility>
#include <exception>


#include <inttypes.h>

#include "table.h"
#include "select.h"
#include "timer.h"
#include "portsorch.h"

#include "port.h"
#include "logger.h"
#include "sai_serialize.h"
// Extern declarations
extern sai_port_api_t *sai_port_api;
extern PortsOrch*       gPortsOrch;
using namespace std::rel_ops;

//string tx_status_name [] = {"ok", "error", "unknown"};

extern "C" {
#include "sai.h"
}

//Defines
#define MONTXORCH_CFG_PERIOD      "port_tx_error_check_period"
#define MONTXORCH_CFG_THRESHOLD       "port_tx_error_threshold"
#define TXPORTMONORCH_KEY_CFG_PERIOD  "GLOBAL_PERIOD"

#define TXPORTMONORCH_SEL_TIMER     "TX_ERR_COUNTERS_POLL"
#define TXMONORCH_FIELD_APPL_STATI      "tx_error_stati"
#define TXMONORCH_FIELD_APPL_TIMESTAMP  "tx_error_timestamp"
#define TXMONORCH_FIELD_APPL_SAIPORTID  "tx_error_portid"
#define TXMONORCH_FIELD_STATE_TX_STATE "tx_status"

#define gettxPortState std::get<0>
#define gettxPortId std::get<1>
#define gettxPortErrCount std::get<2>
#define gettxPortThreshold std::get<3>



/*tx state definition*/
#define TXMONORCH_PORT_STATE_OK         0
#define TXMONORCH_PORT_STATE_ERROR      1
#define TXMONORCH_PORT_STATE_UNKNOWN    2
#define TXMONORCH_PORT_STATE_MAX        3

//Typedefs
using TxErrorStats = std::tuple<bool, sai_object_id_t, uint64_t, uint64_t>;
using TxErrorStatMap = std::unordered_map<std::string, TxErrorStats>;


//Function Declarations
const std::string currentDateTime();




//Class

class MonTxOrch : public Orch
{
    public:
  
    MonTxOrch(TableConnector appDb, TableConnector confDb, TableConnector stateDb);
    ~MonTxOrch(){

    }
    private:
        // Why these params
        void startTimer(uint32_t interval);
        int periodUpdateHandler(const vector<FieldValueTuple>& data); 
        int thresholdUpdateHandler(const string &port, const vector<FieldValueTuple>& data, bool clear);
        
        int pollOnePortErrorStatistics(const string &port, TxErrorStats  &stat);

        void pollErrorStatistics();

        
        void doTask(Consumer& consumer);
        void doTask(SelectableTimer& timer);
        

        //APPL_DB table
        Table m_TxErrorTable;
        //STATE_DB table
        Table m_stateTxErrorTable;
        uint32_t m_pollPeriod=0;


        TxErrorStatMap m_TxPortsErrStat;

     
        SelectableTimer * m_pollTimer;
        
        
};









#endif  /*__MONTXORCH_H */
