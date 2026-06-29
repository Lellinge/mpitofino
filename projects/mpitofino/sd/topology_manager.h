#ifndef MPIP4_TOPOLOGY_MANAGER_H
#define MPIP4_TOPOLOGY_MANAGER_H
#include "state_repository.h"
#include <nlohmann/json.hpp>


class Topology_manager {
    StateRepository& state;
public:
    explicit Topology_manager(StateRepository& state_man);
};


#endif //MPIP4_TOPOLOGY_MANAGER_H