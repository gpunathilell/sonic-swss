#include <gtest/gtest.h>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>
#include <deque>

#include "macaddress.h"
#include "orch.h"
#include "request_parser.h"
#include "orch.h"
#include "ut_helper.h"
#include "mock_orchagent_main.h"
#include "mock_table.h"
#include "port.h"
#define private public
#include "montxorch.h"
#undef private
#include <string>

extern redisReply *mockReply;
extern CrmOrch*  gCrmOrch;
extern string gMySwitchType;
std::deque<KeyOpFieldsValuesTuple>  populateMonCfg(int period,std::string interfaceName,int thresholdValue);

class MonTxOrchTest : public ::testing::Test
{ 
    public:
    std::shared_ptr<swss::DBConnector> m_config_db;
    std::shared_ptr<swss::DBConnector> m_app_db;
    std::shared_ptr<swss::DBConnector> m_state_db;
    shared_ptr<swss::DBConnector> m_chassis_app_db;
    TableConnector stateDbTxErr;
    TableConnector applDbTxErr;
    TableConnector confDbTxErr;
    MonTxOrch *m_MonTxOrch;
    const int portsorch_base_pri = 40;
     vector<table_name_with_pri_t> ports_tables = {
                { APP_PORT_TABLE_NAME, portsorch_base_pri + 5 },
                { APP_VLAN_TABLE_NAME, portsorch_base_pri + 2 },
                { APP_VLAN_MEMBER_TABLE_NAME, portsorch_base_pri },
                { APP_LAG_TABLE_NAME, portsorch_base_pri + 4 },
                { APP_LAG_MEMBER_TABLE_NAME, portsorch_base_pri }
            };
    
    virtual void SetUp() override
    {   
        ASSERT_EQ(sai_route_api, nullptr);
            map<string, string> profile = {
                { "SAI_VS_SWITCH_TYPE", "SAI_VS_SWITCH_TYPE_BCM56850" },
                { "KV_DEVICE_MAC_ADDRESS", "20:03:04:05:06:00" }
            };
        ut_helper::initSaiApi(profile);
        sai_attribute_t attr;
        attr.id = SAI_SWITCH_ATTR_INIT_SWITCH;
        attr.value.booldata = true;
        auto status = sai_switch_api->create_switch(&gSwitchId, 1, &attr);
        ASSERT_EQ(status, SAI_STATUS_SUCCESS);
        m_config_db = std::make_shared<swss::DBConnector>("CONFIG_DB", 0);
        m_app_db = std::make_shared<swss::DBConnector>("APPL_DB", 0);
        m_state_db = std::make_shared<swss::DBConnector>("STATE_DB", 0);
        applDbTxErr= make_pair(m_app_db.get(), /*"TX_ERR_STATE"*/APP_TX_ERR_TABLE_NAME);
        stateDbTxErr=make_pair(m_state_db.get(), /*"TX_ERR_APPL"*/STATE_TX_ERR_TABLE_NAME);
        confDbTxErr= make_pair(m_config_db.get(), /*"TX_ERR_CFG"*/CFG_PORT_TX_ERR_TABLE_NAME);
        if(gMySwitchType == "voq")
                m_chassis_app_db = make_shared<swss::DBConnector>("CHASSIS_APP_DB", 0);
        m_MonTxOrch = new MonTxOrch(applDbTxErr, confDbTxErr, stateDbTxErr);
        TableConnector stateDbSwitchTable(m_state_db.get(), "SWITCH_CAPABILITY");
        TableConnector conf_asic_sensors(m_config_db.get(), CFG_ASIC_SENSORS_TABLE_NAME);
        TableConnector app_switch_table(m_app_db.get(),  APP_SWITCH_TABLE_NAME);
        vector<TableConnector> switch_tables = {
                conf_asic_sensors,
                app_switch_table
        };
        ASSERT_EQ(gSwitchOrch, nullptr);
        gSwitchOrch = new SwitchOrch(m_app_db.get(), switch_tables, stateDbSwitchTable);
        vector<string> flex_counter_tables = {CFG_FLEX_COUNTER_TABLE_NAME};
        auto* flexCounterOrch = new FlexCounterOrch(m_config_db.get(), flex_counter_tables);
        gDirectory.set(flexCounterOrch);
        ASSERT_EQ(gPortsOrch, nullptr);
        gPortsOrch = new PortsOrch(m_app_db.get(), m_state_db.get(), ports_tables, m_chassis_app_db.get());
        auto consumer = unique_ptr<Consumer>(new Consumer(
                new swss::ConsumerStateTable(m_app_db.get(), APP_PORT_TABLE_NAME, 1, 1), gPortsOrch, APP_PORT_TABLE_NAME));
        consumer->addToSync({ { "PortInitDone", EMPTY_PREFIX, { { "", "" } } } });
        static_cast<Orch *>(gPortsOrch)->doTask(*consumer.get());
    }

    void TearDown() override
    {
        delete m_MonTxOrch;
        delete gPortsOrch;
        delete gSwitchOrch;
        Portal::DirectoryInternal::clear(gDirectory);
        EXPECT_TRUE(Portal::DirectoryInternal::empty(gDirectory));
        m_MonTxOrch = nullptr;
        gPortsOrch = nullptr;
        gSwitchOrch = nullptr;
        ut_helper::uninitSaiApi();
    }    
};

std::deque<KeyOpFieldsValuesTuple>  populateMonCfg(int period,std::string interfaceName,int thresholdValue){
    std::deque<KeyOpFieldsValuesTuple>  dqConfDb;
    auto tableKofvt = std::deque<KeyOpFieldsValuesTuple>(
                {                  
                    {
                        interfaceName,
                        SET_COMMAND,
                        {
                            { "port_tx_error_threshold",  to_string(thresholdValue) }
                        }
                    },
                    {
                        TXPORTMONORCH_KEY_CFG_PERIOD,
                        SET_COMMAND,
                        {
                            { "port_tx_error_check_period",  to_string(period) }
                        }
                    }
                }
    );
    return tableKofvt;
}

TEST_F(MonTxOrchTest, test_CfgInit)
{
    std::string interfaceName("Ethernet0");    
    auto consumer = std::unique_ptr<Consumer>(new Consumer(new ConsumerStateTable(m_config_db.get(), CFG_PORT_TX_ERR_TABLE_NAME, 1, 1),m_MonTxOrch, CFG_PORT_TX_ERR_TABLE_NAME));
    consumer->addToSync(populateMonCfg(20,interfaceName,5));
    static_cast<Orch*>(m_MonTxOrch)->doTask(*consumer);
    ASSERT_EQ((m_MonTxOrch)->m_pollPeriod, 20);
    ASSERT_EQ((m_MonTxOrch)->m_TxPortsErrStat.size(), size_t(1));
    ASSERT_NE((m_MonTxOrch)->m_TxPortsErrStat.find(interfaceName),m_MonTxOrch->m_TxPortsErrStat.end());
    ASSERT_EQ(gettxPortThreshold((m_MonTxOrch)->m_TxPortsErrStat[interfaceName]), 5);
}

TEST_F(MonTxOrchTest, test_pollErrorStatistics)
{
    std::string interfaceName("Ethernet4");
    auto consumer = std::unique_ptr<Consumer>(new Consumer(new ConsumerStateTable(m_config_db.get(), CFG_PORT_TX_ERR_TABLE_NAME, 1, 1),m_MonTxOrch, CFG_PORT_TX_ERR_TABLE_NAME));
    consumer->addToSync(populateMonCfg(50,interfaceName,2));
    static_cast<Orch*>(m_MonTxOrch)->doTask(*consumer);
    m_MonTxOrch->pollErrorStatistics();
    std::string zeroString("0");
    ASSERT_EQ((m_MonTxOrch)->m_TxErrorTable.hget("Ethernet4", TXMONORCH_FIELD_APPL_STATI, zeroString), true);
    ASSERT_EQ((m_MonTxOrch)->m_stateTxErrorTable.hget("Ethernet4", TXMONORCH_FIELD_STATE_TX_STATE, zeroString), true);
}