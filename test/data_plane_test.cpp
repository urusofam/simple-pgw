#include <data_plane.h>

#include <gtest/gtest.h>

#include "rate_limited_data_plane.h"

class mock_data_plane_forwarding : public data_plane {
public:
    explicit mock_data_plane_forwarding(control_plane &control_plane) : data_plane(control_plane) {}

    std::unordered_map<boost::asio::ip::address_v4, std::unordered_map<uint32_t, std::vector<Packet>>>
            _forwarded_to_sgw;
    std::unordered_map<boost::asio::ip::address_v4, std::vector<Packet>> _forwarded_to_apn;

protected:
    void forward_packet_to_sgw(boost::asio::ip::address_v4 sgw_addr, uint32_t sgw_dp_teid, Packet &&packet) override {
        _forwarded_to_sgw[sgw_addr][sgw_dp_teid].emplace_back(std::move(packet));
    }

    void forward_packet_to_apn(boost::asio::ip::address_v4 apn_gateway, Packet &&packet) override {
        _forwarded_to_apn[apn_gateway].emplace_back(std::move(packet));
    }
};

class data_plane_test : public ::testing::Test {
public:
    static const inline std::string apn{"test.apn"};
    static const inline auto apn_gw{boost::asio::ip::make_address_v4("127.0.0.1")};
    static const inline auto sgw_addr{boost::asio::ip::make_address_v4("127.1.0.1")};
    static constexpr auto sgw_default_bearer_teid{1};
    static constexpr auto sgw_ded_bearer_teid{2};

    data_plane_test() {
        _control_plane.add_apn(apn, apn_gw);
        _pdn = _control_plane.create_pdn_connection(apn, sgw_addr, sgw_default_bearer_teid);

        _default_bearer = _control_plane.create_bearer(_pdn, sgw_default_bearer_teid);
        _pdn->set_default_bearer(_default_bearer);

        _dedicated_bearer = _control_plane.create_bearer(_pdn, sgw_ded_bearer_teid);
    }

    std::shared_ptr<pdn_connection> _pdn;
    std::shared_ptr<bearer> _default_bearer;
    std::shared_ptr<bearer> _dedicated_bearer;
    control_plane _control_plane;
    mock_data_plane_forwarding _data_plane{_control_plane};
};

TEST_F(data_plane_test, handle_downlink_for_pdn) {
    data_plane::Packet packet1{1, 2, 3};
    _data_plane.handle_uplink(_pdn->get_default_bearer()->get_dp_teid(), {packet1.begin(), packet1.end()});

    data_plane::Packet packet2{4, 5, 6};
    _data_plane.handle_uplink(_dedicated_bearer->get_dp_teid(), {packet2.begin(), packet2.end()});

    data_plane::Packet packet3{7};
    _data_plane.handle_downlink(_pdn->get_ue_ip_addr(), {packet3.begin(), packet3.end()});

    ASSERT_EQ(1, _data_plane._forwarded_to_sgw.size());
    ASSERT_EQ(packet3, _data_plane._forwarded_to_sgw[sgw_addr][sgw_default_bearer_teid][0]);

    ASSERT_EQ(1, _data_plane._forwarded_to_apn.size());
    ASSERT_EQ(2, _data_plane._forwarded_to_apn[apn_gw].size());
    ASSERT_EQ(packet1, _data_plane._forwarded_to_apn[apn_gw][0]);
    ASSERT_EQ(packet2, _data_plane._forwarded_to_apn[apn_gw][1]);
}

TEST_F(data_plane_test, handle_uplink_for_default_bearer) {
    data_plane::Packet packet1{1, 2, 3};
    _data_plane.handle_uplink(_pdn->get_default_bearer()->get_dp_teid(), {packet1.begin(), packet1.end()});

    ASSERT_EQ(1, _data_plane._forwarded_to_apn.size());
    ASSERT_EQ(1, _data_plane._forwarded_to_apn[apn_gw].size());
    ASSERT_EQ(packet1, _data_plane._forwarded_to_apn[apn_gw][0]);
}

TEST_F(data_plane_test, handle_uplink_for_dedicated_bearer) {
    data_plane::Packet packet1{1, 2, 3};
    _data_plane.handle_uplink(_dedicated_bearer->get_dp_teid(), {packet1.begin(), packet1.end()});

    ASSERT_EQ(1, _data_plane._forwarded_to_apn.size());
    ASSERT_EQ(1, _data_plane._forwarded_to_apn[apn_gw].size());
    ASSERT_EQ(packet1, _data_plane._forwarded_to_apn[apn_gw][0]);
}

TEST_F(data_plane_test, didnt_handle_uplink_for_unknown_bearer) {
    data_plane::Packet packet1{1, 2, 3};
    _data_plane.handle_uplink(UINT32_MAX, {packet1.begin(), packet1.end()});

    ASSERT_TRUE(_data_plane._forwarded_to_apn.empty());
}

TEST_F(data_plane_test, didnt_handle_downlink_for_unknown_ue_ip) {
    data_plane::Packet packet1{1, 2, 3};
    _data_plane.handle_downlink(boost::asio::ip::address_v4::any(), {packet1.begin(), packet1.end()});

    ASSERT_TRUE(_data_plane._forwarded_to_apn.empty());
}

class mock_rate_limited_data_plane : public rate_limited_data_plane {
protected:
    void forward_packet_to_sgw(boost::asio::ip::address_v4 sgw_addr, uint32_t sgw_dp_teid, Packet &&packet) override {
        _forwarded_to_sgw[sgw_addr][sgw_dp_teid].emplace_back(std::move(packet));
    }
    void forward_packet_to_apn(boost::asio::ip::address_v4 apn_gateway, Packet &&packet) override {
        _forwarded_to_apn[apn_gateway].emplace_back(std::move(packet));
    }
public:
    explicit mock_rate_limited_data_plane(control_plane &control_plane) : rate_limited_data_plane(control_plane) {}

    std::unordered_map<boost::asio::ip::address_v4, std::unordered_map<uint32_t, std::vector<Packet>>> _forwarded_to_sgw;
    std::unordered_map<boost::asio::ip::address_v4, std::vector<Packet>> _forwarded_to_apn;
};

class rate_limited_data_plane_test : public ::testing::Test {
public:
    static const inline std::string apn{"test.apn"};
    static const inline auto apn_gw{boost::asio::ip::make_address_v4("127.0.0.1")};
    static const inline auto sgw_addr{boost::asio::ip::make_address_v4("127.1.0.1")};
    static constexpr auto sgw_default_bearer_teid{1};
    static constexpr auto sgw_ded_bearer_teid{2};

    rate_limited_data_plane_test() {
        _control_plane.add_apn(apn, apn_gw);
        _pdn = _control_plane.create_pdn_connection(apn, sgw_addr, sgw_default_bearer_teid);

        _default_bearer = _control_plane.create_bearer(_pdn, sgw_default_bearer_teid);
        _pdn->set_default_bearer(_default_bearer);

        _dedicated_bearer = _control_plane.create_bearer(_pdn, sgw_ded_bearer_teid);
    }

    std::shared_ptr<pdn_connection> _pdn;
    std::shared_ptr<bearer> _default_bearer;
    std::shared_ptr<bearer> _dedicated_bearer;
    control_plane _control_plane;
    mock_rate_limited_data_plane _data_plane{_control_plane};
};

TEST_F(rate_limited_data_plane_test, no_rate_limit_allows_all_packets) {
    // Без установки rate limit все пакеты должны проходить
    const size_t packet_count = 10;
    data_plane::Packet packet(1024, 0xFF);  // 1KB пакет

    for (size_t i = 0; i < packet_count; ++i) {
        _data_plane.handle_uplink(_default_bearer->get_dp_teid(), data_plane::Packet(packet));
    }

    EXPECT_EQ(packet_count, _data_plane._forwarded_to_apn[apn_gw].size());
}

TEST_F(rate_limited_data_plane_test, uplink_rate_limit_low) {
    // Устанавливаем очень низкий rate limit для uplink
    rate_limited_data_plane::rate_limit_config config{
        .uplink_rate = 1024,        // 1 KB/s rate
        .uplink_capacity = 1024,      // 1 KB capacity
        .downlink_rate = 1024 * 1024,
        .downlink_capacity = 10 * 1024
    };
    _data_plane.set_rate_limits(_pdn->get_cp_teid(), config);

    // Отправляем 5 пакетов по 1KB подряд
    const size_t packet_count = 5;
    data_plane::Packet packet(1024, 0xFF);

    for (size_t i = 0; i < packet_count; ++i) {
        _data_plane.handle_uplink(_default_bearer->get_dp_teid(), data_plane::Packet(packet));
    }

    // Первый пакет должен пройти, остальные отброшены
    EXPECT_EQ(1, _data_plane._forwarded_to_apn[apn_gw].size());
}

TEST_F(rate_limited_data_plane_test, downlink_rate_limit_low) {
    // Устанавливаем очень низкий rate limit для downlink
    rate_limited_data_plane::rate_limit_config config{
        .uplink_rate = 1024 * 1024,
        .uplink_capacity = 10 * 1024,
        .downlink_rate = 1024,      // 1 KB/s rate
        .downlink_capacity = 1024     // 1 KB capacity
    };
    _data_plane.set_rate_limits(_pdn->get_cp_teid(), config);

    // Отправляем 5 пакетов по 1KB подряд
    const size_t packet_count = 5;
    data_plane::Packet packet(1024, 0xFF);

    for (size_t i = 0; i < packet_count; ++i) {
        _data_plane.handle_downlink(_pdn->get_ue_ip_addr(), data_plane::Packet(packet));
    }

    // Первый пакет должен пройти, остальные отброшены
    EXPECT_EQ(1, _data_plane._forwarded_to_sgw[sgw_addr][sgw_default_bearer_teid].size());
}

TEST_F(rate_limited_data_plane_test, capacity_allows_multiple_packets) {
    // Большая capacity позволяет отправить несколько пакетов подряд
    rate_limited_data_plane::rate_limit_config config{
        .uplink_rate = 1024,        // 1 KB/s rate (низкая скорость)
        .uplink_capacity = 5 * 1024,  // 5 KB capacity
        .downlink_rate = 1024,
        .downlink_capacity = 5 * 1024
    };
    _data_plane.set_rate_limits(_pdn->get_cp_teid(), config);

    // Отправляем 5 пакетов по 1KB - все должны пройти благодаря capacity 5 KB
    const size_t packet_count = 5;
    data_plane::Packet packet(1024, 0xFF);

    for (size_t i = 0; i < packet_count; ++i) {
        _data_plane.handle_uplink(_default_bearer->get_dp_teid(), data_plane::Packet(packet));
    }

    EXPECT_EQ(packet_count, _data_plane._forwarded_to_apn[apn_gw].size());

    // 6-й пакет должен быть отброшен
    _data_plane.handle_uplink(_default_bearer->get_dp_teid(), data_plane::Packet(packet));
    EXPECT_EQ(packet_count, _data_plane._forwarded_to_apn[apn_gw].size());
}

TEST_F(rate_limited_data_plane_test, token_charged_over_time) {
    // Токены должны пополняться со временем
    rate_limited_data_plane::rate_limit_config config{
        .uplink_rate = 10 * 1024,   // 10 KB/s rate
        .uplink_capacity = 1024,       // 1 KB capacity
        .downlink_rate = 10 * 1024,
        .downlink_capacity = 1024
    };
    _data_plane.set_rate_limits(_pdn->get_cp_teid(), config);

    data_plane::Packet packet(1024, 0xFF);  // 1KB пакет

    // Отправляем первый пакет - должен пройти
    _data_plane.handle_uplink(_default_bearer->get_dp_teid(), data_plane::Packet(packet));

    // Второй сразу - должен быть отброшен
    _data_plane.handle_uplink(_default_bearer->get_dp_teid(), data_plane::Packet(packet));

    EXPECT_EQ(1, _data_plane._forwarded_to_apn[apn_gw].size());

    // Ждем 150ms - должно накопиться ~1.5KB токенов
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    // Третий пакет должен пройти (общее количество станет 2)
    _data_plane.handle_uplink(_default_bearer->get_dp_teid(), data_plane::Packet(packet));

    EXPECT_EQ(2, _data_plane._forwarded_to_apn[apn_gw].size());
}

TEST_F(rate_limited_data_plane_test, delete_rate_limits) {
    // Проверяем очистку rate limiters
    rate_limited_data_plane::rate_limit_config config{
        .uplink_rate = 1024,
        .uplink_capacity = 1024,
        .downlink_rate = 1024,
        .downlink_capacity = 1024
    };
    _data_plane.set_rate_limits(_pdn->get_cp_teid(), config);

    // Отправляем пакеты - должны быть ограничены
    data_plane::Packet packet(2048, 0xFF);
    _data_plane.handle_uplink(_default_bearer->get_dp_teid(), data_plane::Packet(packet));

    EXPECT_EQ(0, _data_plane._forwarded_to_apn[apn_gw].size());

    // Очищаем rate limits
    _data_plane.delete_rate_limits(_pdn->get_cp_teid());

    // Теперь пакеты должны проходить без ограничений
    for (int i = 0; i < 5; ++i) {
        _data_plane.handle_uplink(_default_bearer->get_dp_teid(), data_plane::Packet(packet));
        _data_plane.handle_downlink(_pdn->get_ue_ip_addr(), data_plane::Packet(packet));
    }

    EXPECT_EQ(5, _data_plane._forwarded_to_apn[apn_gw].size());
    EXPECT_EQ(5, _data_plane._forwarded_to_sgw[sgw_addr][sgw_default_bearer_teid].size());
}

TEST_F(rate_limited_data_plane_test, multiple_pdns_independent_limits) {
    // Создаем второй PDN с другими ограничениями
    auto pdn2 = _control_plane.create_pdn_connection(apn, sgw_addr, 101);
    auto bearer2 = _control_plane.create_bearer(pdn2, 2);
    pdn2->set_default_bearer(bearer2);

    // PDN1 - медленный
    rate_limited_data_plane::rate_limit_config slow_config{
        .uplink_rate = 1024,
        .uplink_capacity = 1024,
        .downlink_rate = 1024,
        .downlink_capacity = 1024
    };
    _data_plane.set_rate_limits(_pdn->get_cp_teid(), slow_config);

    // PDN2 - быстрый
    rate_limited_data_plane::rate_limit_config fast_config{
        .uplink_rate = 100 * 1024,
        .uplink_capacity = 10 * 1024,
        .downlink_rate = 100 * 1024,
        .downlink_capacity = 10 * 1024
    };
    _data_plane.set_rate_limits(pdn2->get_cp_teid(), fast_config);

    // Отправляем 5KB через каждый PDN
    data_plane::Packet packet(1024, 0xFF);

    for (int i = 0; i < 5; ++i) {
        _data_plane.handle_uplink(_default_bearer->get_dp_teid(), data_plane::Packet(packet));
        _data_plane.handle_uplink(bearer2->get_dp_teid(), data_plane::Packet(packet));
    }

    // PDN1 должен пропустить только 1 пакет, PDN2 - все 5
    EXPECT_EQ(6, _data_plane._forwarded_to_apn[apn_gw].size());
}
