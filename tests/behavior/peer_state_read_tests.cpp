#include "behavior_test_support.h"

// Peer instance-state read (ABI v31): a behavior reads ANOTHER entity's behavior
// data by handle + module name (wz_instance_state_of). This is the read half of
// the "other points to an entity that carries its own data" model -- e.g. a tank's
// collision handler reading a projectile's {shooter, damage} off the `other`
// handle. The reader + owner agree on the struct via a shared header; the engine
// defines no schema.
namespace
{
    struct PeerSourceState
    {
        uint32_t marker = 0u;
        float    value = 0.0f;
    };

    struct PeerReaderProbe
    {
        uint32_t           calls = 0;
        WzBehaviorEntityId source_entity = WZ_INVALID_BEHAVIOR_ENTITY;
        bool     read_present = false;   // wz_instance_state_of returned non-null
        uint32_t read_marker = 0u;
        float    read_value = 0.0f;
        bool     wrong_module_null = false;  // wrong module on the source -> null
        bool     wrong_entity_null = false;  // an entity without that behavior -> null
    };

    PeerReaderProbe* g_peer_reader_probe = nullptr;

    void peer_reader_event_handler(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        void* user)
    {
        auto* probe = static_cast<PeerReaderProbe*>(user);
        ASSERT_NE(probe, nullptr);
        ASSERT_NE(facts, nullptr);
        ASSERT_NE(event, nullptr);
        if (!wz_is_event(event, WZ_EVENT_FRAME_UPDATE)) {
            return;
        }
        ++probe->calls;

        if (auto* s = wz_instance_state_of<PeerSourceState>(
                facts, probe->source_entity, "peer_source"))
        {
            probe->read_present = true;
            probe->read_marker = s->marker;
            probe->read_value = s->value;
        }
        // Right entity, wrong module -> null.
        probe->wrong_module_null =
            wz_instance_state_of<PeerSourceState>(
                facts, probe->source_entity, "not_a_module") == nullptr;
        // The reader's own node has no "peer_source" behavior -> null.
        probe->wrong_entity_null =
            wz_instance_state_of<PeerSourceState>(
                facts, wz_self(event), "peer_source") == nullptr;
    }

    void peer_source_noop(
        const WzBehaviorFrameFacts*, const WzBehaviorEvent*, void*)
    {
    }

    uint8_t register_peer_pack(WzBehaviorPluginApi* api)
    {
        if (!api || !api->register_module || !g_peer_reader_probe) {
            return 0;
        }
        // The source is just a state carrier -- a no-op handler so its binding
        // dispatches cleanly; its state is populated by the test below.
        const uint8_t a = api->register_module(
            api->user, "peer_source", peer_source_noop, nullptr);
        const uint8_t b = api->register_module(
            api->user, "peer_reader", peer_reader_event_handler,
            g_peer_reader_probe);
        return a && b;
    }
}

TEST(PeerStateRead, ReadsAnotherEntitysBehaviorStateByHandleAndModule)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    PeerReaderProbe probe{};
    probe.source_entity = 4u;
    g_peer_reader_probe = &probe;
    ASSERT_TRUE(plugins.register_static_pack(registry, register_peer_pack));

    constexpr RuntimeEntityId kSource = 4u;
    constexpr RuntimeEntityId kReader = 7u;

    // Entity 4 carries "peer_source"; entity 7 carries "peer_reader".
    SceneInstance scene = scene_with_behavior(kSource, "peer_source", "src");
    scene.behaviors[0].component.binding_id = "peer_source_4";
    scene.behaviors.push_back(SceneComponentRecord<BehaviorComponent>{
        .node = kReader,
        .component = BehaviorComponent{
            .binding_id = "peer_reader_7",
            .module = "peer_reader",
            .name = "rdr",
            .enabled = true,
            .events = { "frame.update" },
            .channel_mask =
                wz::engine::behavior::channel_mask_for_token("frame.update"),
        },
    });

    // Populate the source's instance state with a known marker.
    wz::engine::assets::BehaviorStateStorage state{};
    auto* block = state.allocate_instance_state(
        "peer_source_4",
        static_cast<uint32_t>(sizeof(PeerSourceState)),
        static_cast<uint32_t>(alignof(PeerSourceState)));
    ASSERT_NE(block, nullptr);
    ASSERT_NE(block->data, nullptr);
    auto* src = static_cast<PeerSourceState*>(block->data);
    src->marker = 0xABCD1234u;
    src->value = 12.5f;

    wz::engine::FrameStorage frame_storage{};
    BehaviorFrameContext context{
        .frame_storage = &frame_storage,
        .scene = &scene,
        .behavior_state = &state,
        .commands = &frame_storage.behavior_commands,
    };

    dispatch_behaviors(scene, registry, context);

    EXPECT_GE(probe.calls, 1u);
    EXPECT_TRUE(probe.read_present);           // read the source's state by handle
    EXPECT_EQ(probe.read_marker, 0xABCD1234u); // ...and it's the right block
    EXPECT_FLOAT_EQ(probe.read_value, 12.5f);
    EXPECT_TRUE(probe.wrong_module_null);      // wrong module -> null (safe miss)
    EXPECT_TRUE(probe.wrong_entity_null);      // no such behavior -> null

    g_peer_reader_probe = nullptr;
}
