//
// Created by aleksei on 16.07.25.
//

#include "rate_limited_data_plane.h"

token_bucket::token_bucket(double rate_, double capacity_)
    : rate(rate_), capacity(capacity_), tokens(capacity_), last_update(std::chrono::steady_clock::now()) {}

// Пытаемся потратить токены
bool token_bucket::spend_tokens(double tokens_) {
    std::lock_guard<std::mutex> lock(mutex);

    // Вычисляем сколько времени прошло
    auto now = std::chrono::steady_clock::now();
    std::chrono::duration<double> passed_time = now - last_update;

    // Добавляем новые токены пропорционально прошедшему времени
    double new_tokens = passed_time.count() * rate;
    tokens = std::min(tokens + new_tokens, capacity);
    last_update = now;

    // Проверяем, достаточно ли токенов
    if (tokens >= tokens_) {
        tokens -= tokens_;
        return true;
    }
    return false;
}

rate_limited_data_plane::rate_limited_data_plane(control_plane& cp) : data_plane(cp) {}

void rate_limited_data_plane::set_rate_limits(uint32_t cp_teid, rate_limit_config &config) {
    // Находим PDN
    auto pdn = _control_plane.find_pdn_by_cp_teid(cp_teid);
    if (!pdn) {
        return;
    }

    // Получаем ip адресс UE
    boost::asio::ip::address_v4 ue_ip = pdn->get_ue_ip_addr();

    // Устанавливаем лимиты для PDN соединения
    uplink_limiters[cp_teid] = std::make_unique<token_bucket>(config.uplink_rate, config.uplink_capacity);
    downlink_limiters[ue_ip] = std::make_unique<token_bucket>(config.downlink_rate, config.downlink_capacity);
}

void rate_limited_data_plane::delete_rate_limits(uint32_t cp_teid) {
    // Находим PDN
    auto pdn = _control_plane.find_pdn_by_cp_teid(cp_teid);
    if (!pdn) {
        return;
    }

    // Получаем ip адресс UE
    boost::asio::ip::address_v4 ue_ip = pdn->get_ue_ip_addr();

    // Удаляем лимиты для PDN соединения
    uplink_limiters.erase(cp_teid);
    downlink_limiters.erase(ue_ip);
}

void rate_limited_data_plane::handle_uplink(uint32_t dp_teid, Packet &&packet) {
    auto bearer = _control_plane.find_bearer_by_dp_teid(dp_teid);
    if (!bearer) {
        return;
    }

    auto pdn = bearer->get_pdn_connection();
    if (!pdn) {
        return;
    }

    // Проверяем rate limit
    auto it = uplink_limiters.find(pdn->get_cp_teid());
    if (it != uplink_limiters.end()) {
        if (!it->second->spend_tokens(packet.size())) {
            return;
        }
    }

    forward_packet_to_apn(pdn->get_apn_gw(), std::move(packet));
}

void rate_limited_data_plane::handle_downlink(const boost::asio::ip::address_v4 &ue_ip, Packet &&packet) {
    auto pdn = _control_plane.find_pdn_by_ip_address(ue_ip);
    if (!pdn) {
        return;
    }

    auto default_bearer = pdn->get_default_bearer();
    if (!default_bearer) {
        return;
    }

    // Проверяем rate limit
    auto it = downlink_limiters.find(ue_ip);
    if (it != downlink_limiters.end()) {
        if (!it->second->spend_tokens(packet.size())) {
            return;
        }
    }

    forward_packet_to_sgw(pdn->get_sgw_address(), default_bearer->get_sgw_dp_teid(), std::move(packet));
}


