// Dedicated-server replication composition for ScienceAndTheology.
//
// This module is the sole game owner of the direct socket transport. The
// shared ScienceAndTheologySimulationSession remains transport-neutral, while
// this wrapper places inbound replication before game systems and outbound
// replication after the scheduler's fixed-tick barrier.

#pragma once

#include "engine/simulation_session.h"
#include "game/client/game_session_config.h"
#include "game/network/game_server_replication_handler.h"
#include "game/simulation/science_and_theology_simulation_session.h"

#include <memory>
#include <string>

namespace snt::engine {
class FixedTickContext;
class SimulationServices;
class SimulationWorldSession;
}

namespace snt::network {
class LanDiscoveryResponder;
class ReplicationService;
class TcpUdpReplicationTransport;
}

namespace snt::game::replication {
class GameServerCommandSink;
class GameServerPlayerBedService;
class GameServerPlayerDeathService;
class GameServerPlayerGraveStore;
class GameServerPlayerInteractionService;
class GameServerPlayerLifecycle;
class GameServerPlayerMovement;
class GameServerQuestEventService;
class GameServerQuestBookReplication;
class GameServerPlayerReplication;
class GameServerPlayerRespawnResolver;
class GameServerPlayerState;
}

namespace snt::game {

// Server-only startup inputs. The password is deliberately outside
// GameSessionConfig because the latter is loaded from a package also shipped
// to clients. An empty password leaves the server open.
struct GameServerSessionOptions {
    GameSessionConfig config;
    std::string server_password;
};

class ScienceAndTheologyServerSession final : public snt::engine::ISimulationSession {
public:
    explicit ScienceAndTheologyServerSession(GameServerSessionOptions options);
    ~ScienceAndTheologyServerSession() override;

    snt::core::Expected<void> register_content(snt::engine::SimulationServices& services) override;
    snt::core::Expected<void> create_world(snt::engine::SimulationWorldSession& world) override;
    snt::core::Expected<void> fixed_tick(snt::engine::FixedTickContext& context) override;
    snt::core::Expected<void> after_fixed_tick(snt::engine::FixedTickContext& context) override;
    void shutdown() noexcept override;

private:
    GameSessionConfig config_;
    std::string server_password_;
    bool server_password_required_ = false;
    ScienceAndTheologySimulationSession simulation_session_;
    snt::engine::SimulationServices* services_ = nullptr;
    std::unique_ptr<replication::GameServerCommandSink> command_sink_;
    std::unique_ptr<replication::GameServerPlayerState> player_state_;
    std::unique_ptr<replication::GameServerPlayerMovement> player_movement_;
    std::unique_ptr<replication::GameServerPlayerBedService> player_beds_;
    std::unique_ptr<replication::GameServerPlayerGraveStore> player_graves_;
    std::unique_ptr<replication::GameServerPlayerRespawnResolver> player_respawn_;
    std::unique_ptr<replication::GameServerPlayerDeathService> player_death_;
    std::unique_ptr<replication::GameServerPlayerInteractionService> player_interactions_;
    std::unique_ptr<replication::GameServerQuestEventService> quest_events_;
    std::unique_ptr<replication::GameServerQuestBookReplication> quest_book_replication_;
    std::unique_ptr<replication::GameServerPlayerReplication> player_replication_;
    std::unique_ptr<replication::GameServerPlayerLifecycle> player_lifecycle_;
    std::unique_ptr<replication::IGamePeerAuthenticator> peer_authenticator_;
    std::unique_ptr<replication::GameServerReplicationHandler> replication_handler_;
    std::unique_ptr<snt::network::TcpUdpReplicationTransport> transport_;
    std::unique_ptr<snt::network::ReplicationService> replication_service_;
    std::unique_ptr<snt::network::LanDiscoveryResponder> lan_discovery_responder_;
};

}  // namespace snt::game
