#pragma once

#include <data_plane.h>
#include <chrono>
#include <mutex>
#include <unordered_map>

class token_bucket {
    double rate;
    double capacity;
    double tokens;
    std::chrono::steady_clock::time_point last_update;
    mutable std::mutex mutex;

public:
    token_bucket(double rate, double capacity);
    bool spend_tokens(double tokens);
};

class rate_limited_data_plane : public data_plane {
    std::unordered_map<uint32_t, std::unique_ptr<token_bucket>> uplink_limiters;
    std::unordered_map<boost::asio::ip::address_v4, std::unique_ptr<token_bucket>> downlink_limiters;

protected:
    void forward_packet_to_sgw(boost::asio::ip::address_v4 sgw_addr, uint32_t sgw_dp_teid, Packet &&packet) override {}
    void forward_packet_to_apn(boost::asio::ip::address_v4 apn_gateway, Packet &&packet) override {}

public:
    explicit rate_limited_data_plane(control_plane& cp);

    struct rate_limit_config {
        size_t uplink_rate;
        size_t uplink_capacity;
        size_t downlink_rate;
        size_t downlink_capacity;
    };

    void set_rate_limits(uint32_t cp_teid, rate_limit_config& config);
    void handle_uplink(uint32_t dp_teid, Packet&& packet) override;
    void handle_downlink(const boost::asio::ip::address_v4& ue_ip, Packet&& packet) override;
    void delete_rate_limits(uint32_t cp_teid);
};