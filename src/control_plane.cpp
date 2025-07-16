#include <control_plane.h>
#include <bearer.h>
#include <algorithm>
#include <random>

static uint32_t generate_teid() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<uint32_t> dis(1, UINT32_MAX - 1);
    return dis(gen);
}

std::shared_ptr<pdn_connection> control_plane::find_pdn_by_cp_teid(uint32_t cp_teid) const {
    auto it = _pdns.find(cp_teid);
    if (it != _pdns.end()) {
        return it->second;
    }
    return nullptr;
}

std::shared_ptr<pdn_connection> control_plane::find_pdn_by_ip_address(const boost::asio::ip::address_v4 &ip) const {
    auto it = _pdns_by_ue_ip_addr.find(ip);
    if (it != _pdns_by_ue_ip_addr.end()) {
        return it->second;
    }
    return nullptr;
}

std::shared_ptr<bearer> control_plane::find_bearer_by_dp_teid(uint32_t dp_teid) const {
    auto it = _bearers.find(dp_teid);
    if (it != _bearers.end()) {
        return it->second;
    }
    return nullptr;
}

std::shared_ptr<pdn_connection> control_plane::create_pdn_connection(const std::string &apn,
                                                                    boost::asio::ip::address_v4 sgw_addr,
                                                                    uint32_t sgw_cp_teid) {
    // Проверяем, существует ли APN
    auto apn_it = _apns.find(apn);
    if (apn_it == _apns.end()) {
        return nullptr;
    }

    // Генерируем уникальный CP TEID для PGW
    uint32_t cp_teid = generate_teid();
    while (_pdns.find(cp_teid) != _pdns.end()) {
        cp_teid = generate_teid();
    }

    // Выделяем IP адрес для UE
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<uint8_t> dis(1, 254);

    boost::asio::ip::address_v4 ue_ip(boost::asio::ip::address_v4::bytes_type{
        10, static_cast<uint8_t>(dis(gen)), static_cast<uint8_t>(dis(gen)), static_cast<uint8_t>(dis(gen))
    });

    // Убеждаемся, что IP адрес уникален
    while (_pdns_by_ue_ip_addr.find(ue_ip) != _pdns_by_ue_ip_addr.end()) {
        ue_ip = boost::asio::ip::address_v4(boost::asio::ip::address_v4::bytes_type{
            10, static_cast<uint8_t>(dis(gen)), static_cast<uint8_t>(dis(gen)), static_cast<uint8_t>(dis(gen))
        });
    }

    // Создаем PDN connection
    auto pdn = pdn_connection::create(cp_teid, apn_it->second, ue_ip);
    pdn->set_sgw_addr(sgw_addr);
    pdn->set_sgw_cp_teid(sgw_cp_teid);

    // Сохраняем
    _pdns[cp_teid] = pdn;
    _pdns_by_ue_ip_addr[ue_ip] = pdn;

    return pdn;
}

void control_plane::delete_pdn_connection(uint32_t cp_teid) {
    auto it = _pdns.find(cp_teid);
    if (it == _pdns.end()) {
        return;
    }

    auto pdn = it->second;

    // Удаляем все bearers этого PDN
    std::vector<uint32_t> bearer_teids;
    for (const auto& [teid, bearer] : _bearers) {
        if (bearer->get_pdn_connection() == pdn) {
            bearer_teids.push_back(teid);
        }
    }

    for (uint32_t teid : bearer_teids) {
        delete_bearer(teid);
    }

    // Удаляем из индекса по IP адресу
    _pdns_by_ue_ip_addr.erase(pdn->get_ue_ip_addr());

    // Удаляем сам PDN
    _pdns.erase(it);
}

std::shared_ptr<bearer> control_plane::create_bearer(const std::shared_ptr<pdn_connection> &pdn, uint32_t sgw_teid) {
    if (!pdn) {
        return nullptr;
    }

    // Генерируем уникальный DP TEID для bearer
    uint32_t dp_teid = generate_teid();
    while (_bearers.find(dp_teid) != _bearers.end()) {
        dp_teid = generate_teid();
    }

    // Создаем bearer
    auto new_bearer = std::make_shared<bearer>(dp_teid, *pdn);
    new_bearer->set_sgw_dp_teid(sgw_teid);

    // Добавляем bearer в PDN
    pdn->add_bearer(new_bearer);

    // Сохраняем
    _bearers[dp_teid] = new_bearer;

    return new_bearer;
}

void control_plane::delete_bearer(uint32_t dp_teid) {
    auto it = _bearers.find(dp_teid);
    if (it == _bearers.end()) {
        return;
    }

    auto bearer_to_delete = it->second;
    auto pdn = bearer_to_delete->get_pdn_connection();

    if (pdn) {
        // Удаляем bearer из PDN
        pdn->remove_bearer(dp_teid);
    }

    // Удаляем из мапы
    _bearers.erase(it);
}

void control_plane::add_apn(std::string apn_name, boost::asio::ip::address_v4 apn_gateway) {
    _apns[apn_name] = apn_gateway;
}