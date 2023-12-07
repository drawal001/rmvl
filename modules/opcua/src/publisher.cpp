/**
 * @file pubsub.cpp
 * @author zhaoxi (535394140@qq.com)
 * @brief OPC UA 发布者
 * @version 1.0
 * @date 2023-12-01
 *
 * @copyright Copyright 2023 (c), zhaoxi
 *
 */

#include "rmvl/opcua/publisher.hpp"

#ifdef UA_ENABLE_PUBSUB

#include <open62541/plugin/log_stdout.h>
#include <open62541/plugin/pubsub_udp.h>
#include <open62541/server_config_default.h>

#ifdef UA_ENABLE_PUBSUB_MQTT
#include <open62541/plugin/pubsub_mqtt.h>
#endif // UA_ENABLE_PUBSUB_MQTT

#include "rmvl/core/util.hpp"
#include "rmvl/opcua/utilities.hpp"

#include "rmvlpara/opcua.hpp"

namespace rm
{

// #ifndef UA_ENABLE_PUBSUB_MQTT
// static UA_PubSubTransportLayer UA_PubSubTransportLayerMQTT_nullptr()
// {
//     RMVL_Error(RMVL_StsInvFmt, "MQTT transport layer is not enabled, please use another transport layer like UDP.");
//     return UA_PubSubTransportLayer{};
// }
// #endif // UA_ENABLE_PUBSUB_MQTT

// // 传输协议 URI 映射
// static constexpr const char *tp_profile[] = {
//     "http://opcfoundation.org/UA-Profile/Transport/pubsub-udp-uadp",
//     "http://opcfoundation.org/UA-Profile/Transport/pubsub-mqtt-uadp",
//     "http://opcfoundation.org/UA-Profile/Transport/pubsub-mqtt-json",
// };

// // 传输层 URI 映射
// static constexpr UA_PubSubTransportLayer (*tp_layer[])() = {
//     UA_PubSubTransportLayerUDPMP,
// #ifdef UA_ENABLE_PUBSUB_MQTT
//     UA_PubSubTransportLayerMQTT, UA_PubSubTransportLayerMQTT
// #else
//     UA_PubSubTransportLayerMQTT_nullptr, UA_PubSubTransportLayerMQTT_nullptr
// #endif // UA_ENABLE_PUBSUB_MQTT
// };

/************************************************************************************/
/************************************** 发布者 **************************************/

Publisher<TransportID::UDP_UADP>::Publisher(const std::string &pub_name, const std::string &address, uint16_t port,
                                            const std::vector<UserConfig> &users) : Server(port, users), _name(pub_name)
{
    //////////////////// 添加连接配置 ////////////////////
    UA_ServerConfig_addPubSubTransportLayer(UA_Server_getConfig(_server), UA_PubSubTransportLayerUDPMP());
    UA_PubSubConnectionConfig connect_config{};
    connect_config.name = UA_String_fromChars((_name + "Connection").c_str());
    connect_config.transportProfileUri = UA_String_fromChars("http://opcfoundation.org/UA-Profile/Transport/pubsub-udp-uadp");
    connect_config.enabled = UA_TRUE;
    UA_NetworkAddressUrlDataType address_url{UA_STRING_NULL, UA_String_fromChars(address.c_str())};
    UA_Variant_setScalarCopy(&connect_config.address, &address_url, &UA_TYPES[UA_TYPES_NETWORKADDRESSURLDATATYPE]);
    // 用哈希值作为发布者 ID
    connect_config.publisherId.numeric = _strhash(_name + "Connection") % 0x8000000u;
    auto status = UA_Server_addPubSubConnection(_server, &connect_config, &_connection_id);
    if (status != UA_STATUSCODE_GOOD)
    {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "Failed to add connection, \"%s\"", UA_StatusCode_name(status));
        return;
    }
    //////////// 添加 PublishedDataSet (PDS) /////////////
    UA_PublishedDataSetConfig pds_config{};
    pds_config.publishedDataSetType = UA_PUBSUB_DATASET_PUBLISHEDITEMS;
    pds_config.name = UA_String_fromChars((_name + "PublishedDataSet").c_str());
    auto pds_status = UA_Server_addPublishedDataSet(_server, &pds_config, &_pds_id);
    if (pds_status.addResult != UA_STATUSCODE_GOOD)
    {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "Failed to add published dataset, \"%s\"",
                     UA_StatusCode_name(pds_status.addResult));
        return;
    }
}

static UA_DataSetFieldConfig getPDS(const PublishedDataSet &pd)
{
    UA_DataSetFieldConfig dsf_config{};
    dsf_config.dataSetFieldType = UA_PUBSUB_DATASETFIELD_VARIABLE;
    dsf_config.field.variable.fieldNameAlias = UA_String_fromChars(pd.name.c_str());
    dsf_config.field.variable.promotedField = false;
    dsf_config.field.variable.publishParameters.publishedVariable = pd.node_id;
    dsf_config.field.variable.publishParameters.attributeId = UA_ATTRIBUTEID_VALUE;
    return dsf_config;
}

bool Publisher<TransportID::UDP_UADP>::publish(const std::vector<PublishedDataSet> &datas, double duration)
{
    ////////////////////// 前置条件 //////////////////////
    if (_server == nullptr)
        RMVL_Error(RMVL_StsNullPtr, "Server is nullptr.");
    if (UA_NodeId_isNull(&_connection_id))
        return false;

    ////////////// 添加 DataSetField (DSF) ///////////////
    for (const auto &pds : datas)
    {
        auto dsf_config = getPDS(pds);
        UA_NodeId dsf_node_id;
        auto result = UA_Server_addDataSetField(_server, _pds_id, &dsf_config, &dsf_node_id);
        if (result.result != UA_STATUSCODE_GOOD)
        {
            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "Failed to add dataset field, name: \"%s\", status code: \"%s\"",
                         pds.name.c_str(), UA_StatusCode_name(result.result));
            return false;
        }
    }

    //////////////// 添加 WriterGroup (WG) ///////////////
    UA_WriterGroupConfig wg_config{};
    wg_config.name = UA_String_fromChars((_name + "WriterGroup").c_str());
    wg_config.publishingInterval = duration;
    wg_config.enabled = UA_FALSE;
    wg_config.writerGroupId = _strhash(_name + "WriterGroup") % 0x8000u;
    wg_config.encodingMimeType = UA_PUBSUB_ENCODING_UADP;
    wg_config.messageSettings.encoding = UA_EXTENSIONOBJECT_DECODED;
    wg_config.messageSettings.content.decoded.type = &UA_TYPES[UA_TYPES_UADPWRITERGROUPMESSAGEDATATYPE];
    // 将写入组的消息设置更改为在网络消息的发布者 ID、写入组 ID 和数据集写入器 ID 中发送
    UA_UadpWriterGroupMessageDataType wg_msg{};
    wg_msg.networkMessageContentMask = UA_UADPNETWORKMESSAGECONTENTMASK_PUBLISHERID |
                                       UA_UADPNETWORKMESSAGECONTENTMASK_GROUPHEADER |
                                       UA_UADPNETWORKMESSAGECONTENTMASK_WRITERGROUPID |
                                       UA_UADPNETWORKMESSAGECONTENTMASK_PAYLOADHEADER;
    wg_config.messageSettings.content.decoded.data = &wg_msg;
    auto status = UA_Server_addWriterGroup(_server, _connection_id, &wg_config, &_wg_id);
    if (status != UA_STATUSCODE_GOOD)
    {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "Failed to add writer group, \"%s\"", UA_StatusCode_name(status));
        return false;
    }
    status = UA_Server_setWriterGroupOperational(_server, _wg_id);
    if (status != UA_STATUSCODE_GOOD)
    {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "Failed to set writer group operational, \"%s\"",
                     UA_StatusCode_name(status));
        return false;
    }
    ////////////// 添加 DataSetWriter (DSW) //////////////
    UA_DataSetWriterConfig dsw_config{};
    dsw_config.name = UA_String_fromChars((_name + "DataSetWriter").c_str());
    dsw_config.dataSetWriterId = _strhash(_name + "DataSetWriter") % 0x8000u;
    dsw_config.keyFrameCount = para::opcua_param.KEY_FRAME_COUNT;
    status = UA_Server_addDataSetWriter(_server, _wg_id, _pds_id, &dsw_config, &_dsw_id);
    if (status != UA_STATUSCODE_GOOD)
    {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "Failed to add dataset writer, \"%s\"", UA_StatusCode_name(status));
        return false;
    }
    return true;
}

} // namespace rm

#endif // UA_ENABLE_PUBSUB
