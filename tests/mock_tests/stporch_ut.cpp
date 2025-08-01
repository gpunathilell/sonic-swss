#include <gtest/gtest.h>
#include <gmock/gmock.h>

#define private public // make Directory::m_values available to clean it.
#include "directory.h"
#undef private
#define protected public
#include "orch.h"
#undef protected
#include "ut_helper.h"
#include "dbconnector.h"
#include "mock_orchagent_main.h"
#include "mock_sai_api.h"
#include "mock_orch_test.h"
#include "mock_table.h"
#define private public
#include "stporch.h"
#undef private
#include "mock_sai_stp.h"


namespace stporch_test
{
    using namespace std;
    using namespace swss;
    using namespace mock_orch_test;
    using ::testing::StrictMock;

    using ::testing::_;
    using ::testing::Return;
    using ::gStpOrch;

    sai_status_t _ut_stub_sai_set_vlan_attribute(_In_ sai_object_id_t vlan_oid,
                    _In_ const sai_attribute_t *attr)
    {
        return SAI_STATUS_SUCCESS;
    }

    sai_status_t _ut_stub_sai_flush_fdb_entries(_In_ sai_object_id_t switch_id,
                    _In_ uint32_t attr_count, _In_ const sai_attribute_t *attr_list)
    {
        return SAI_STATUS_SUCCESS;
    }

    sai_status_t _ut_stub_sai_get_switch_attribute(
        _In_ sai_object_id_t switch_id,
        _In_ uint32_t attr_count,
        _Inout_ sai_attribute_t *attr_list)
    {
        return SAI_STATUS_SUCCESS;
        if (attr_count == 2)
        {
            if (attr_list[0].id == SAI_SWITCH_ATTR_DEFAULT_STP_INST_ID)
            {
                attr_list[0].value.oid = 0;
            }
            if (attr_list[1].id == SAI_SWITCH_ATTR_MAX_STP_INSTANCE)
            {
                attr_list[1].value.u32 = 510;
            }
        }
        return SAI_STATUS_SUCCESS;
    }
    

    class StpOrchTest : public MockOrchTest {
    protected:
        void ApplyInitialConfigs()
        {
            Table port_table = Table(m_app_db.get(), APP_PORT_TABLE_NAME);
            Table vlan_table = Table(m_app_db.get(), APP_VLAN_TABLE_NAME);
            Table vlan_member_table = Table(m_app_db.get(), APP_VLAN_MEMBER_TABLE_NAME);

            auto ports = ut_helper::getInitialSaiPorts();
            port_table.set(ETHERNET0, ports[ETHERNET0]);
            port_table.set(ETHERNET4, ports[ETHERNET4]);
            port_table.set(ETHERNET8, ports[ETHERNET8]);
            port_table.set("PortConfigDone", { { "count", to_string(1) } });
            port_table.set("PortInitDone", { {} });

            vlan_table.set(VLAN_1000, { { "admin_status", "up" },
                                        { "mtu", "9100" },
                                        { "mac", "00:aa:bb:cc:dd:ee" } });
            vlan_member_table.set(
                VLAN_1000 + vlan_member_table.getTableNameSeparator() + ETHERNET0,
                { { "tagging_mode", "untagged" } });

            gPortsOrch->addExistingData(&port_table);
            gPortsOrch->addExistingData(&vlan_table);
            gPortsOrch->addExistingData(&vlan_member_table);
            static_cast<Orch *>(gPortsOrch)->doTask();
        }
        void PostSetUp() override
        {
            vector<string> tableNames =
                {"STP_TABLE",
                "STP_VLAN_INSTANCE_TABLE",
                "STP_PORT_STATE_TABLE",
                "STP_FASTAGEING_FLUSH_TABLE",
                "STP_INST_PORT_FLUSH_TABLE"};
            _hook_sai_switch_api();
            gStpOrch = new StpOrch(m_app_db.get(), m_state_db.get(), tableNames);
        }
        void PreTearDown() override
        {
            delete gStpOrch;
            gStpOrch = nullptr;
            _unhook_sai_switch_api();
        }

        sai_stp_api_t ut_sai_stp_api;
        sai_stp_api_t *org_sai_stp_api;

        void _hook_sai_stp_api()
        {
            ut_sai_stp_api = *sai_stp_api;
            org_sai_stp_api = sai_stp_api;
            sai_stp_api = &ut_sai_stp_api;
        }

        void _unhook_sai_stp_api()
        {
            sai_stp_api = org_sai_stp_api;
        }

        sai_vlan_api_t ut_sai_vlan_api;
        sai_vlan_api_t *org_sai_vlan_api;

        void _hook_sai_vlan_api()
        {
            ut_sai_vlan_api = *sai_vlan_api;
            org_sai_vlan_api = sai_vlan_api;
            ut_sai_vlan_api.set_vlan_attribute = _ut_stub_sai_set_vlan_attribute;
            sai_vlan_api = &ut_sai_vlan_api;
        }

        void _unhook_sai_vlan_api()
        {
            sai_vlan_api = org_sai_vlan_api;
        }

        sai_fdb_api_t ut_sai_fdb_api;
        sai_fdb_api_t *org_sai_fdb_api;
        void _hook_sai_fdb_api()
        {
            ut_sai_fdb_api = *sai_fdb_api;
            org_sai_fdb_api = sai_fdb_api;
            ut_sai_fdb_api.flush_fdb_entries = _ut_stub_sai_flush_fdb_entries;
            sai_fdb_api = &ut_sai_fdb_api;
        }

        void _unhook_sai_fdb_api()
        {
            sai_fdb_api = org_sai_fdb_api;
        }

        sai_switch_api_t ut_sai_switch_api;
        sai_switch_api_t *pold_sai_switch_api;
        void _hook_sai_switch_api()
        {
            ut_sai_switch_api = *sai_switch_api;
            pold_sai_switch_api = sai_switch_api;
            ut_sai_switch_api.get_switch_attribute = _ut_stub_sai_get_switch_attribute;
            sai_switch_api = &ut_sai_switch_api;
        }
        void _unhook_sai_switch_api()
        {
            sai_switch_api = pold_sai_switch_api;
        }
    };

    TEST_F(StpOrchTest, TestAddRemoveStpPort) {
        _hook_sai_stp_api();
        _hook_sai_vlan_api();
        _hook_sai_fdb_api();

        StrictMock<MockSaiStp> mock_sai_stp_;
        mock_sai_stp = &mock_sai_stp_;
        sai_stp_api->create_stp = mock_create_stp;
        sai_stp_api->remove_stp = mock_remove_stp;
        sai_stp_api->create_stp_port = mock_create_stp_port;
        sai_stp_api->remove_stp_port = mock_remove_stp_port;
        sai_stp_api->set_stp_port_attribute = mock_set_stp_port_attribute;

        Port port;
        Port port1;
        sai_uint16_t stp_instance = 1;
        sai_object_id_t stp_port_oid = 67890;
        sai_object_id_t stp_oid = 98765;
        bool result;

        ASSERT_TRUE(gPortsOrch->getPort(ETHERNET0, port));
        ASSERT_TRUE(gPortsOrch->getPort(ETHERNET4, port1));

        EXPECT_CALL(mock_sai_stp_,
            create_stp(_, _, _, _)).WillOnce(::testing::DoAll(::testing::SetArgPointee<0>(stp_oid),
                                        ::testing::Return(SAI_STATUS_SUCCESS)));
        result = gStpOrch->addVlanToStpInstance(VLAN_1000, stp_instance);
        ASSERT_TRUE(result);

        EXPECT_CALL(mock_sai_stp_,
            create_stp_port(_, _, 3, _)).WillOnce(::testing::DoAll(::testing::SetArgPointee<0>(stp_port_oid),
                                        ::testing::Return(SAI_STATUS_SUCCESS)));
        EXPECT_CALL(mock_sai_stp_,
            set_stp_port_attribute(_,_)).WillOnce(::testing::Return(SAI_STATUS_SUCCESS));
        port.m_bridge_port_id = 1234;
        result = gStpOrch->updateStpPortState(port, stp_instance, STP_STATE_FORWARDING);
        ASSERT_TRUE(result);

        result = gStpOrch->stpVlanFdbFlush(VLAN_1000);
        ASSERT_TRUE(result);

        EXPECT_CALL(mock_sai_stp_,
            remove_stp_port(_)).WillOnce(::testing::Return(SAI_STATUS_SUCCESS));
        result = gStpOrch->removeStpPort(port, stp_instance);
        ASSERT_TRUE(result);

        EXPECT_CALL(mock_sai_stp_,
            create_stp_port(_, _, 3, _)).WillOnce(::testing::DoAll(::testing::SetArgPointee<0>(stp_port_oid),
                                        ::testing::Return(SAI_STATUS_SUCCESS)));
        EXPECT_CALL(mock_sai_stp_,
            set_stp_port_attribute(_,_)).WillOnce(::testing::Return(SAI_STATUS_SUCCESS));
        port1.m_bridge_port_id = 1111;
        result = gStpOrch->updateStpPortState(port1, stp_instance, STP_STATE_BLOCKING);
        ASSERT_TRUE(result);

        EXPECT_CALL(mock_sai_stp_,
            remove_stp_port(_)).WillOnce(::testing::Return(SAI_STATUS_SUCCESS));
        result = gStpOrch->removeStpPorts(port1);
        ASSERT_TRUE(result);

        EXPECT_CALL(mock_sai_stp_,
            remove_stp(_)).WillOnce(::testing::Return(SAI_STATUS_SUCCESS));
        result = gStpOrch->removeVlanFromStpInstance(VLAN_1000, stp_instance);
        ASSERT_TRUE(result);

        std::deque<KeyOpFieldsValuesTuple> entries;
        entries.push_back({"Vlan1000", "SET", { {"stp_instance", "1"}}});
        EXPECT_CALL(mock_sai_stp_,
            create_stp(_, _, _, _)).WillOnce(::testing::DoAll(::testing::SetArgPointee<0>(stp_oid),
                                        ::testing::Return(SAI_STATUS_SUCCESS)));

        auto consumer = dynamic_cast<Consumer *>(gStpOrch->getExecutor("STP_VLAN_INSTANCE_TABLE"));
        consumer->addToSync(entries);
        static_cast<Orch *>(gStpOrch)->doTask();

        entries.clear();
        EXPECT_CALL(mock_sai_stp_,
            create_stp_port(_, _, 3, _)).WillOnce(::testing::DoAll(::testing::SetArgPointee<0>(stp_port_oid),
                                        ::testing::Return(SAI_STATUS_SUCCESS)));
        EXPECT_CALL(mock_sai_stp_,
            set_stp_port_attribute(_,_)).WillOnce(::testing::Return(SAI_STATUS_SUCCESS));
        entries.push_back({"Ethernet0:1", "SET", { {"state", "4"}}});
        consumer = dynamic_cast<Consumer *>(gStpOrch->getExecutor("STP_PORT_STATE_TABLE"));
        consumer->addToSync(entries);
        static_cast<Orch *>(gStpOrch)->doTask();

        entries.clear();
        entries.push_back({"Ethernet0:1", "SET", { {"state", "true"}}});
        consumer = dynamic_cast<Consumer *>(gStpOrch->getExecutor("STP_FASTAGEING_FLUSH_TABLE"));
        consumer->addToSync(entries);
        static_cast<Orch *>(gStpOrch)->doTask();

        entries.clear();
        entries.push_back({"Ethernet0:1", "DEL", { {} }});
        EXPECT_CALL(mock_sai_stp_,
            remove_stp_port(_)).WillOnce(::testing::Return(SAI_STATUS_SUCCESS));
        consumer = dynamic_cast<Consumer *>(gStpOrch->getExecutor("STP_PORT_STATE_TABLE"));
        consumer->addToSync(entries);
        static_cast<Orch *>(gStpOrch)->doTask();

        entries.clear();
        entries.push_back({"Vlan1000", "DEL", { {} }});
        EXPECT_CALL(mock_sai_stp_,
            remove_stp(_)).WillOnce(::testing::Return(SAI_STATUS_SUCCESS));
        consumer = dynamic_cast<Consumer *>(gStpOrch->getExecutor("STP_VLAN_INSTANCE_TABLE"));
        consumer->addToSync(entries);
        static_cast<Orch *>(gStpOrch)->doTask();

        entries.clear();
        entries.push_back({"1:Ethernet0", "SET", { {"state", "true"} }});
        consumer = dynamic_cast<Consumer*>(gStpOrch->getExecutor(APP_STP_INST_PORT_FLUSH_TABLE_NAME));
        consumer->addToSync(entries);
        static_cast<Orch *>(gStpOrch)->doTask();

        _unhook_sai_stp_api();
        _unhook_sai_vlan_api();
        _unhook_sai_fdb_api();
    }
}
