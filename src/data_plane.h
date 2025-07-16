#pragma once

#include <control_plane.h>

#include <boost/asio/ip/address.hpp>

#include <cstdint>
#include <vector>

class data_plane {
public:
    using Packet = std::vector<uint8_t>;

    explicit data_plane(control_plane &control_plane);
    virtual ~data_plane() = default;

    virtual void handle_uplink(uint32_t dp_teid, Packet &&packet);
    virtual void handle_downlink(const boost::asio::ip::address_v4 &ue_ip, Packet &&packet);

protected:
    virtual void forward_packet_to_sgw(boost::asio::ip::address_v4 sgw_addr, uint32_t sgw_dp_teid, Packet &&packet) = 0;
    virtual void forward_packet_to_apn(boost::asio::ip::address_v4 apn_gateway, Packet &&packet) = 0;

    control_plane &_control_plane;
};
