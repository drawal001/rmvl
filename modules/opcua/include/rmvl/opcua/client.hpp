/**
 * @file client.hpp
 * @author zhaoxi (535394140@qq.com)
 * @brief OPC UA 客户端
 * @version 2.2
 * @date 2024-03-07
 *
 * @copyright Copyright 2024 (c), zhaoxi
 *
 */

#pragma once

#include <array>

#include <open62541/client_subscriptions.h>

#include "object.hpp"
#include "view.hpp"

namespace rm
{

//! @addtogroup opcua
//! @{

//! @example samples/opcua/opcua_client.cpp OPC UA 客户端例程

//! OPC UA 客户端视图
class ClientView
{
public:
    ClientView() = default;

    /**
     * @brief 创建不占有生命周期的 OPC UA 客户端视图，在 OPC UA 方法节点中使用特别有效
     *
     * @param[in] client OPC UA 客户端指针
     */
    ClientView(UA_Client *client) : _client(client) {}

    ClientView &operator=(UA_Client *const client)
    {
        _client = client;
        return *this;
    }

    //! 获取 OPC UA 客户端指针
    UA_Client *get() const { return _client; }

    /**
     * @brief 获取路径搜索必要信息
     * @brief 需要配合管道运算符 `|` 完成路径搜索
     * @code{.cpp}
     * auto dst_mode = src_node | clt.find("person") | clt.find("name");
     * @endcode
     *
     * @param[in] browse_name 浏览名
     * @param[in] ns 命名空间索引，默认为 `1`
     * @return 目标节点信息
     * @retval fnic `[_client, browse_name]` 元组
     */
    inline FindNodeInClient find(std::string_view browse_name, uint16_t ns = 1U) const { return {_client, browse_name, ns}; }

    /**
     * @brief 从指定的变量节点读数据
     *
     * @param[in] node 既存的变量节点的 `NodeId`
     * @return 读出的用 `rm::Variable` 表示的数据，未成功读取则返回空
     */
    Variable read(const NodeId &node) const;

    /**
     * @brief 给指定的变量节点写数据
     *
     * @param[in] node 既存的变量节点的 `NodeId`
     * @param[in] val 待写入的数据
     * @return 是否写入成功
     */
    bool write(const NodeId &node, const Variable &val) const;

private:
    UA_Client *_client{nullptr}; //!< 客户端指针
};

/**
 * @brief 数据变更通知回调函数
 *
 * @param[in] client_view 客户端视图，指代当前客户端
 * @param[in] value 数据发生变更后的变量
 */
using DataChangeNotificationCallback = std::function<void(ClientView, const Variable &)>;

/**
 * @brief 事件通知回调函数
 *
 * @param[in] client_view 客户端视图，指代当前客户端
 * @param[in] event_fields 事件数据
 */
using EventNotificationCallback = std::function<void(ClientView, InputVariables)>;

//! OPC UA 客户端
class Client
{
public:
    /****************************** 通用配置 ******************************/

    /**
     * @brief 创建新的客户端对象，并建立连接
     *
     * @param[in] address 连接地址，形如 `opc.tcp://127.0.0.1:4840`
     * @param[in] usr 用户信息
     */
    Client(std::string_view address, const UserConfig &usr = {});
    ~Client();

    Client(const Client &) = delete;
    Client(Client &&) = default;

    Client &operator=(const Client &) = delete;
    Client &operator=(Client &&) = default;

    operator ClientView() const { return _client; }

    //! 是否成功创建客户端并成功连接到服务器
    inline bool ok() const { return _client != nullptr; }

    /**
     * @brief 在网络上监听并处理到达的异步响应，同时进行内部维护、安全通道的更新和订阅管理
     * @brief
     * - 执行事件循环，等效于 ROS/ROS2 工具包中的 `ros::spin()` 以及 `rclcpp::spin()`
     */
    void spin() const;

    /**
     * @brief 在网络上监听并处理到达的异步响应，同时进行内部维护、安全通道的更新和订阅管理
     * @brief
     * - 处理当前已到来的事件，等效于 ROS/ROS2 工具包中的 `ros::spinOnce()` 以及 `rclcpp::spin_some()`
     */
    void spinOnce() const;

    //! 断开与服务器的连接
    bool shutdown();

    /****************************** 路径搜索 ******************************/

    /**
     * @brief 获取路径搜索必要信息
     * @brief 需要配合管道运算符 `|` 完成路径搜索
     * @code{.cpp}
     * auto dst_mode = src_node | clt.find("person") | clt.find("name");
     * @endcode
     *
     * @param[in] browse_name 浏览名
     * @param[in] ns 命名空间索引，默认为 `1`
     * @return 目标节点信息
     * @retval fnic `[_client, browse_name]` 元组
     */
    inline FindNodeInClient find(std::string_view browse_name, uint16_t ns = 1U) const { return {_client, browse_name, ns}; }

    /****************************** 功能配置 ******************************/

    /**
     * @brief 从指定的变量节点读数据
     *
     * @param[in] node 既存的变量节点的 `NodeId`
     * @return 读出的用 `rm::Variable` 表示的数据，未成功读取则返回空
     */
    Variable read(const NodeId &node) const;

    /**
     * @brief 给指定的变量节点写数据
     *
     * @param[in] node 既存的变量节点的 `NodeId`
     * @param[in] val 待写入的数据
     * @return 是否写入成功
     */
    bool write(const NodeId &node, const Variable &val) const;

    /**
     * @brief 在客户端调用指定对象节点中的方法
     *
     * @param[in] obj_node 对象节点
     * @param[in] name 方法名
     * @param[in] inputs 输入参数列表
     * @param[out] outputs 输出参数列表
     * @return 是否成功完成当前操作
     */
    bool call(const NodeId &obj_node, const std::string &name, const std::vector<Variable> &inputs, std::vector<Variable> &outputs) const;

    /**
     * @brief 在客户端调用 ObjectsFolder 中的方法
     *
     * @param[in] name 方法名 `browse_name`
     * @param[in] inputs 输入参数列表
     * @param[out] outputs 输出参数列表
     * @return 是否成功完成当前操作
     */
    inline bool call(const std::string &name, const std::vector<Variable> &inputs, std::vector<Variable> &outputs) const { return call(nodeObjectsFolder, name, inputs, outputs); }

    /**
     * @brief 添加 OPC UA 视图节点 ViewNode 至 `ViewsFolder` 中
     *
     * @param[in] view `rm::View` 表示的视图
     * @return 添加至服务器后，对应视图节点的唯一标识 `NodeId`
     */
    NodeId addViewNode(const View &view) const;

    /**
     * @brief 创建变量节点监视项，以实现订阅节点的功能
     * @brief
     * - 服务器在设定的采样频率 `opcua_param.SAMPLING_INTERVAL` 下监视变量，若发生更改会尝试发出通知，通知的发送频率受到
     *   `opcua_param.PUBLISHING_INTERVAL` 控制。当客户端收到通知时，执行 `on_change` 回调函数
     * @brief
     * - 类似于 ROS 中的订阅话题，这里是订阅变量节点
     *
     * @param[in] node 待监视节点的 `NodeId`
     * @param[in] on_change 数据变更可调用对象
     * @param[in] queue_size 通知存放的队列大小，若队列已满，新的通知会覆盖旧的通知，默认为 `10`
     * @return 变量节点监视创建成功？
     */
    bool monitor(NodeId node, DataChangeNotificationCallback on_change, uint32_t queue_size = 10);

    /**
     * @brief 创建事件监视项，以实现事件的订阅功能
     *
     * @param[in] node 待监视节点的 `NodeId`
     * @param[in] names 关注的事件属性名列表，参考 Event::data()
     * @param[in] on_event 事件回调函数
     * @return 事件监视创建成功？
     */
    bool monitor(NodeId node, const std::vector<std::string> &names, EventNotificationCallback on_event);

    /**
     * @brief 移除监视项
     *
     * @param[in] node 待移除监视项的节点号
     * @return 是否成功移除监视项
     */
    bool remove(NodeId node);

private:
    //! 客户端指针
    UA_Client *_client{nullptr};
    //! 节点号监视项映射表 `[NodeId : [SubId, MonitorId]]`
    std::unordered_map<UA_UInt32, std::array<UA_UInt32, 2>> _monitor_map;
    //! 数据变更通知回调函数
    std::vector<std::unique_ptr<DataChangeNotificationCallback>> _dccb_gc{};
    //! 事件通知回调函数
    std::vector<std::unique_ptr<EventNotificationCallback>> _encb_gc{};
};

//! OPC UA 客户端定时器
class ClientTimer final
{
public:
    using Callback = std::function<void(ClientView)>; //!< 定时器回调函数

    /**
     * @brief 创建 OPC UA 客户端定时器
     *
     * @param[in] cv 客户端视图
     * @param[in] period 定时器周期，单位：毫秒 `ms`
     * @param[in] callback 定时器回调函数
     */
    ClientTimer(ClientView sv, double period, Callback callback);

    ClientTimer(const ClientTimer &) = delete;
    ClientTimer(ClientTimer &&) = default;

    ClientTimer &operator=(const ClientTimer &) = delete;
    ClientTimer &operator=(ClientTimer &&) = default;

    ~ClientTimer() { cancel(); }

    //! 取消定时器
    void cancel();

private:
    ClientView _cv; //!< 客户端视图
    Callback _cb;   //!< 定时器回调函数
    uint64_t _id{}; //!< 定时器 ID
};

//! @} opcua

} // namespace rm