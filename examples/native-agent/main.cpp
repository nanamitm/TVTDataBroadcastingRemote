#include <nlohmann/json.hpp>
#include "RemoteAgent.h"
#include <iostream>

int main() {
    TvtRemote::Agent agent;
    agent.UpdateState("Native C++ TVTest demo", "Native pipe test", true, "demo");
    if (!agent.Start()) return 1;
    for (;;) {
        TvtRemote::Agent::Command command;
        while (agent.Next(command)) {
            std::cout << "key=" << command.key << std::endl;
            agent.Complete(command, true);
        }
        Sleep(25);
    }
}
