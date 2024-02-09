#include "montxorch.h"









MonTxOrch::MonTxOrch(TableConnector appDb, TableConnector confDb, TableConnector stateDb):
    Orch(confDb.first,confDb.second),
    m_TxErrorTable(appDb.first,appDb.second),
    m_stateTxErrorTable(stateDb.first,stateDb.second),
    m_pollPeriod(0)
{
    SWSS_LOG_ENTER();
    m_pollTimer = new SelectableTimer(timespec { .tv_sec = 0, .tv_nsec = 0 });
    Orch::addExecutor(new ExecutableTimer(m_pollTimer, this, MONTXPORTORCH_SEL_TIMER););
    SWSS_LOG_NOTICE("MonTxOrch initialized with the following tables %s %s %s\n",stateDb.second.c_str(),appDb.second.c_str());



}


void MonTxOrch::startTimer(uint32_t interval)
{
    SWSS_LOG_ENTER();

    try
    {
        auto timespecInterval = timespec { .tv_sec = interval , .tv_nsec = 0 };
        SWSS_LOG_INFO("MonTxOrch::startTimer with executor: %p\n", m_pollTimer);
        m_pollPeriod = timespecInterval;
        m_pollTimer->setInterval(timespecInterval);
        m_pollTimer->stop();
        m_pollTimer->start();
        
    }
    catch (...)
    {
        SWSS_LOG_ERROR("MonTxOrch::startTimer function failure\n");
    }

}

int MonTxOrch::periodUpdateHandler(const vector<FieldValueTuple>& data)
{
    bool restart = false;
	bool shutdown = false;
    uint32_t newPollPeriod = 0;
    


    SWSS_LOG_ENTER();
    for (auto iter_ : data)
    {
        try {
            
            if (fvField(iter_) == MONTXORCH_CFG_PERIOD)
            {
                
                newPollPeriod = to_uint<uint32_t>(fvValue(i));
                restart |= (m_pollPeriod != newPollPeriod);  //Differenc in current period and existing poll period causes restart
                SWSS_LOG_INFO("MonTxOrch::periodUpdateHandler Update Monitoring Period to %u\n", periodToSet);
            }
            else
            {
                SWSS_LOG_ERROR("MonTxOrch::periodUpdateHandler Invalid field type provided: %s\n", fvField(iter_).c_str());
                return -1;
            }
        }
        catch (...) {
            SWSS_LOG_ERROR("MonTxOrch::periodUpdateHandler Function failure\n");
            return -1;
        }
    }
    
    if (restart)
    {
        startTimer(newPollPeriod);
        SWSS_LOG_ERROR("MonTxOrch::periodUpdateHandler startTimer function called\n");
    }

    return 0;


}
int MonTxOrch::thresholdUpdateHandler(const string &port, const vector<FieldValueTuple>& data, bool clear){
    SWSS_LOG_ENTER();
    try {
        if (clear)
        {
            m_TxPortsErrStat.erase(port);
            m_TxErrorTable.del(port);
            m_stateTxErrorTable.del(port);
            
            SWSS_LOG_INFO("TX_ERR threshold cleared for port %s\n", port.c_str());
        }
        else
            {
                for (auto iter_ : data)
                {
                    if (MONTXORCH_CFG_THRESHOLD == fvField(iter_))
                    {
                        TxErrorStats &tesTuple = m_TxPortsErrStat[port];
                        if (gettxPortId(tesTuple) == 0)
                        {
                          
                            Port saiport;
                            if (gPortsOrch->getPort(port, saiport))
                            {
                                gettxPortId(tesTuple) = saiport.m_port_id;
                            }
                            gettxPortState(tesTuple) = TXMONORCH_PORT_STATE_UNKNOWN;
                        }
                        gettxPortThreshold(tesTuple) = to_uint<uint64_t>(fvValue(iter_));
                        SWSS_LOG_INFO("MonTxOrch::thresholdUpdateHandler threshold updated to %ld for port %s\n", gettxPortThreshold(tesTuple), port.c_str());
                    }
                    else
                    {
                        SWSS_LOG_ERROR("MonTxOrch::thresholdUpdateHandler Unknown field type %s threshold for %s\n", fvField(i).c_str(), port.c_str());
                        return -1;
                    }
                }
            }
    }
    catch (...) {
        SWSS_LOG_ERROR("Fail to startTimer handle periodic update\n");
    }

    return 0;
}


void MonTxOrch::doTask(Consumer& consumer)
{
    int rc = 0;

    SWSS_LOG_ENTER();
   

    if (!gPortsOrch->isPortReady())
    {
        SWSS_LOG_INFO("MonTxOrch::doTask Ports not ready\n");
        return;
    }
    
    for (auto it = consumer.m_toSync.begin();it != consumer.m_toSync.end();it++)
    {
        KeyOpFieldsValuesTuple t = it->second;

        string key = kfvKey(t);
        string op = kfvOp(t);
        vector<FieldValueTuple> fvs = kfvFieldsValues(t);
        rc = -1;

        SWSS_LOG_INFO("MonTxOrch::doTask %s operation %s set %s del %s\n", key.c_str(),op.c_str(), SET_COMMAND, DEL_COMMAND);
        if (key == TXMONORCH_KEY_CFG_PERIOD)
        {
            if (op == SET_COMMAND)
            {
                rc = periodUpdateHandler(fvs);
            }
            else
            {
                SWSS_LOG_ERROR("Unknown operation type %s when set period\n", op.c_str());
            }
        }
        else
        {
            if (op == SET_COMMAND)
            {
                
                rc = thresholdUpdateHandler(key, fvs, false);
            }
            else if (op == DEL_COMMAND)
            {
                rc = thresholdUpdateHandler(key, fvs, true);
            }
            else
            {
                SWSS_LOG_ERROR("Unknown operation type %s when set threshold\n", op.c_str());
            }
        }

        if (rc)
        {
            SWSS_LOG_ERROR("Handle configuration update failed index %s\n", key.c_str());
        }

        consumer.m_toSync.erase(it++);
    }
}


int TxMonOrch::pollOnePortErrorStatistics(const string &port, TxErrorStats  &stat)
{
    uint64_t txErrStatistics = 0,txErrStatLasttime = gettxPortErrCount(stat),txErrStatThreshold = gettxPortThreshold(stat);
    int tx_error_state,tx_error_state_lasttime = gettxPortState(stat);

    SWSS_LOG_ENTER();
    static const vector<sai_stat_id_t> txErrStatId = {SAI_PORT_STAT_IF_OUT_ERRORS};
    uint64_t tx_err = -1;
    
    sai_port_api->get_port_stats(gettxPortId(stat),
                                    static_cast<uint32_t>(txErrStatId.size()),
                                    txErrStatId.data(),
                                    &tx_err);
    txErrStatistics = tx_err;
    SWSS_LOG_INFO("TX_ERR_POLL: got port %s tx_err stati %ld, lasttime %ld threshold %ld\n", 
                    port.c_str(), txErrStatistics, txErrStatLasttime, txErrStatThreshold);


    if (txErrStatistics - txErrStatLasttime > txErrStatThreshold)
    {
        tx_error_state = TXMONORCH_PORT_STATE_ERROR;
    }
    else
    {
        tx_error_state = TXMONORCH_PORT_STATE_OK;
    }
    if (tx_error_state != tx_error_state_lasttime)
    {
        gettxPortState(stat) = tx_error_state;
        vector<FieldValueTuple> fvs;
		if (tx_error_state < TXMONORCH_PORT_STATE_MAX){
            fvs.emplace_back(TXMONORCH_FIELD_STATE_TX_STATE, tx_status_name[tx_error_state]);
        }
            
		else{
            fvs.emplace_back(TXMONORCH_FIELD_STATE_TX_STATE, "invalid");
        }
            
        m_stateTxErrorTable.set(port, fvs);
        SWSS_LOG_INFO("TX_ERR_CFG: port %s state changed to %d, push to db\n", port.c_str(), tx_error_state);
    }

    //refresh the local copy of last time statistics
    gettxPortErrCount(stat) = txErrStatistics;

    return 0;
}

void MonTxOrch::pollErrorStatistics()
{
    SWSS_LOG_ENTER();

    KeyOpFieldsValuesTuple portEntry;

    for (auto i : m_PortsTxErrStat)
    {
        vector<FieldValueTuple> fields;
        int rc;

        SWSS_LOG_INFO("TX_ERR_APPL: port %s tx_err_stat %ld, before get\n", i.first.c_str(),
                        gettxPortErrCount(i.second));
        rc = pollOnePortErrorStatistics(i.first, i.second);
        if (rc != 0)
            SWSS_LOG_ERROR("TX_ERR_APPL: got port %s tx_err_stat failed %d\n", i.first.c_str(), rc);
        fields.emplace_back(TXMONORCH_FIELD_APPL_STATI, to_string(gettxPortErrCount(i.second)));
        fields.emplace_back(TXMONORCH_FIELD_APPL_TIMESTAMP, "0");
        fields.emplace_back(TXMONORCH_FIELD_APPL_SAIPORTID, to_string(gettxPortId(i.second)));
        m_TxErrorTable.set(i.first, fields);
        SWSS_LOG_INFO("TX_ERR_APPL: port %s tx_err_stat %ld, push to db\n", i.first.c_str(),
                        gettxPortErrCount(i.second));
    }

    m_TxErrorTable.flush();
    m_stateTxErrorTable.flush();
    SWSS_LOG_INFO("TX_ERR_APPL: flushing tables\n");
}

void MonTxOrch::doTask(SelectableTimer &timer)
{
    SWSS_LOG_INFO("TxMonOrch doTask selectable timer\n");
    //for each ports, check the statisticis 
    pollErrorStatistics();
}