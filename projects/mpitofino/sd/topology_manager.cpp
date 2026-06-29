#include "topology_manager.h"

#include <fstream>

Topology_manager::Topology_manager(StateRepository &state_man) : state(state_man) {
    std::ifstream f("topology.json");
    nlohmann::json data = nlohmann::json::parse(f);
    state.set_is_root_switch(data["root"].get<bool>());
    state.set_upstream_port(data["upstream_port"].get<uint16_t>());
    if (not state.is_root_switch()) {
        state.set_switch_to_switch_src_mac(MacAddr(data["src_mac_addr"].get<std::string>()));
        state.set_switch_to_switch_dst_mac(MacAddr(data["dst_mac_addr"].get<std::string>()));
        state.set_switch_to_switch_src_ipv4(IPv4Addr(data["src_ipv4_addr"].get<std::string>()));
        state.set_switch_to_switch_dst_ipv4(IPv4Addr(data["dst_ipv4_addr"].get<std::string>()));
    }
}
