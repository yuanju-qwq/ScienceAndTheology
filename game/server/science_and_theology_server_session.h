// Dedicated-server replication composition for ScienceAndTheology.
//
// This module is the sole game owner of the direct socket transport. The
// shared ScienceAndTheologySimulationSession remains transport-neutral, while
// this wrapper places inbound replication before game systems and outbound
// replication after the scheduler's fixed-tick barrier.

#pragma once

#include "engine/simulation_session.h"
#include "game/client/game_session_config.h"
#include "game/simulation/science_and_theology_simulation_session.h"

#include <memory>

namespace snt::engine {
class FixedTickContext;
class SimulationServices;
class SimulationWorldSession;
}

namespace snt::network {
class ReplicationService;
class TcpUdpReplicationTransport;
}

namespace snt::game {

class ScienceAndTheologyServerSession final : public snt::engine::ISimulationSession {
public:
    explicit ScienceAndTheologyServerSession(GameSessionConfig config);
    ~ScienceAndTheologyServerSession() override;

    snt::core::Expected<void> register_content(snt::engine::SimulationServices& services) override;
    snt::core::Expected<void> create_world(snt::engine::SimulationWorldSession& world) override;
    snt::core::Expected<void> fixed_tick(snt::engine::FixedTickContext& context) override;
    snt::core::Expected<void> after_fixed_tick(snt::engine::FixedTickContext& context) override;
    void shutdown() noexcept override;

private:
    class ReplicationHandler;

    GameSessionConfig config_;
    ScienceAndTheologySimulationSession simulation_session_;
    std::unique_ptr<ReplicationHandler> replication_handler_;
    std::unique_ptr<snt::network::TcpUdpReplicationTransport> transport_;
    std::unique_ptr<snt::network::ReplicationService> replication_service_;
};

}  // namespace snt::game
