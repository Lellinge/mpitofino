#ifndef MPIP4_TOPOLOGY_MANAGER_H
#define MPIP4_TOPOLOGY_MANAGER_H
#include "state_repository.h"


class Topology_manager {
    StateRepository& state;
public:
    Topology_manager(StateRepository& state_man);
};


#endif //MPIP4_TOPOLOGY_MANAGER_H