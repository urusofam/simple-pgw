#include <data_plane.h>
#include <bearer.h>

data_plane::data_plane(control_plane &control_plane) : _control_plane(control_plane) {}

void data_plane::handle_uplink(uint32_t dp_teid, Packet &&packet) {
    // Находим bearer по DP TEID
    auto bearer = _control_plane.find_bearer_by_dp_teid(dp_teid);
    if (!bearer) {
        return;
    }

    // Получаем PDN connection
    auto pdn = bearer->get_pdn_connection();
    if (!pdn) {
        return;
    }

    // Пересылаем пакет на APN Gateway
    forward_packet_to_apn(pdn->get_apn_gw(), std::move(packet));
}

void data_plane::handle_downlink(const boost::asio::ip::address_v4 &ue_ip, Packet &&packet) {
    // Находим PDN connection по IP адресу
    auto pdn = _control_plane.find_pdn_by_ip_address(ue_ip);
    if (!pdn) {
        return;
    }

    // Получаем default bearer для downlink трафика
    auto default_bearer = pdn->get_default_bearer();
    if (!default_bearer) {
        return;
    }

    // Пересылаем пакет на SGW через default bearer
    forward_packet_to_sgw(pdn->get_sgw_address(),
                         default_bearer->get_sgw_dp_teid(),
                         std::move(packet));
}