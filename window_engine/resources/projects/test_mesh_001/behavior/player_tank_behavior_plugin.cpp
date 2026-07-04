#include "player_tank.h"
#include "tank_drive.h"

namespace
{
    static const char* kTankEvents[] = {
    "input.*","self.start"
    };


    static constexpr float movement_factor = 0.1;

    // ── Engine grain synth ────────────────────────────────────────────────────
    // The engine "sounds" node (state->engine_audio, authored node "3") hosts a
    // grain cloud whose bank is the wav/Engines folder, sorted -> clip indices:
    //   0 = Engines_C.wav  1 = Engines_CC.wav  2 = Engines_D.wav  3 = Engines_F.wav
    // We drive it by RPM: each rpm level picks one clip and we crossfade the WHOLE
    // preset (a "program") toward it over ~1 s when the level changes. Tweak the
    // grain feel (size, density, position, jitter, pan) in make_engine_program.

    // ~1 second at a 48 kHz device (grain ramps are counted in audio frames).
    static constexpr float kEngineCrossfadeFrames = 48000.0f;

    // RPM level -> which bank clip voices the engine (per player_tank.h mapping).
    // Index 0 is unused (rpm 0 = engine off / silent).
    static constexpr uint32_t kRpmClip[5] = { 0u, 0u, 2u, 3u, 1u };

    // Build the complete grain preset for an rpm level. rpm 0 = silent (all
    // weights 0, gain 0); 1..4 = a slow grain from the middle of that level's clip.
    static WzGrainProgram make_engine_program(uint8_t rpm)
    {
        WzGrainProgram p = {};          // zero-init: weights all 0 = silent
        p.weight_count = 4u;            // C, CC, D, F
        p.gain = 0.9f;                  // <-- tweak: engine loudness
        p.density = 7.0f * (1.0f + float(rpm));              // <-- tweak: grains/sec (slow overlap)
        p.position = 0.5f;              // middle of the clip
        p.pitch = 1.0f;
        p.grain_ms = 300.0f ;            // <-- tweak: "slow" grain length (ms)
        p.position_jitter = 0.05f;      // <-- tweak: playhead wander
        p.pitch_jitter_semitones = 0.1f;
        p.pan_center = 0.0f;
        p.pan_spread = 0.15f;           // <-- tweak: stereo width
        p.window_param = 0.4f;
        p.window = WZ_GRAIN_WINDOW_GAUSSIAN;
        p.blend_rate = 0.0f;            // weights select the clip, not the LFO
        p.blend_depth = 0.0f;

        if (rpm == 0u) {
            p.gain = 0.0f;              // engine off (weights already all 0)
            return p;
        }
        if (rpm > 4u) { rpm = 4u; }
        p.weights[kRpmClip[rpm]] = 1.0f;
        return p;
    }

    // Quantize total tread effort (both treads, either direction) into rpm 0..4.
    static uint8_t compute_rpm_level(const PlayerTankState* state)
    {
        const float effort = (fabsf(state->left_tread_speed)
                            + fabsf(state->right_tread_speed)) * 0.5f;  // ~0..1
        if (effort < 0.05f) {
            return 1u;                  // idle deadzone = engine off
        }
        int level = (int)(effort * 4.0f + 0.5f);
        if (level < 1) { level = 1; }
        if (level > 4) { level = 4; }
        return (uint8_t)level;
    }

    // Recompute rpm from tread speed and, when the level changed, crossfade the
    // engine cloud's program toward that level. The first push snaps (ramp 0) so we
    // don't hear the renderable's authored all-clips default fade out.
    static void update_engine_rpm(
        const WzBehaviorFrameFacts* facts,
        PlayerTankState* state)
    {
        if (state->engine_audio == WZ_INVALID_BEHAVIOR_ENTITY) {
            return;
        }
        const uint8_t rpm = compute_rpm_level(state);
        if ((int)rpm == state->applied_rpm_level) {
            return;
        }
        const float ramp = (state->applied_rpm_level < 0)
            ? 0.0f : kEngineCrossfadeFrames;
        state->applied_rpm_level = (int8_t)rpm;
        state->rpm_level = rpm;
        const WzGrainProgram prog = make_engine_program(rpm);
        wz_write_grain_program(facts, state->engine_audio, &prog, ramp);
    }

    void tank_init(
        const WzBehaviorInitFacts* facts,
        WzBehaviorEntityId self,
        void*)
    {
        PlayerTankState* state = wz_instance_state<PlayerTankState>(facts);

        if (state) {
            uint8_t result = wz_find_entity_by_authored_id(
                facts, "clipmap_landscape", &state->terrain);
            wz_log_infof(facts, "[tank init] find terrain: %u", result);

            // The player is a spawned prefab now, so the camera + engine-sounds are
            // our OWN children with conflict-free ids -- resolve them RELATIVE to
            // self (by name), not by a scene-global authored id.
            result = wz_find_descendant_by_name(
                facts, self, "camera", &state->canon_audio);
            wz_log_infof(facts, "[tank init] find canon audio: %u", result);

            result = wz_find_descendant_by_name(
                facts, self, "engine_sounds", &state->engine_audio);
            wz_log_infof(facts, "[tank init] find engine audio: %u", result);
        }
    }


    static void try_fire_canon(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        PlayerTankState* state)
    {
        (void)event;
        if (state->ammo > 0) {
            wz_log_infof(facts, "[tank] we have ammo %u", state->ammo);
            state->ammo--;
            if (state->canon_audio != WZ_INVALID_BEHAVIOR_ENTITY) {
                wz_log_info(facts, "[tank] played the canon");
                wz_write_play_sound_named(facts, state->canon_audio, "Canon_a");
            }
        }
    }

    void on_event(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        void*)
    {

        if (!facts || !event) {
            return;
        }

        auto* state = static_cast<PlayerTankState*>(
            wz_get_instance_state(facts));
        if (!state) {
            return;
        }

        // Decider/actuator split: if THIS node also hosts a quantum_agent (add the
        // "quantum_agent" behavior to the tank node and give it a `goal`), it
        // deliberates a binary disposition on its OWN clock. Here we just READ its
        // committed decision -- a cheap cached read, no cognition runs here -- and
        // react when the wave function collapses. The player still drives; the
        // quantum sub-mind governs an auxiliary call (here, whether to engage). The
        // read no-ops cleanly when no quantum_agent is authored on the node.
        WzAgentDecision decision{};
        if (wz_self_agent_decision(facts, event, &decision)
            && decision.committed != state->last_decision)
        {
            state->last_decision = decision.committed;
            if (decision.committed == 0) {
                wz_log_infof(
                    facts,
                    "[tank] quantum mind committed: ENGAGE (z=%.2f)",
                    decision.marginal);
                // React audibly so the collapse is observable in play.
                if (state->canon_audio != WZ_INVALID_BEHAVIOR_ENTITY) {
                    wz_write_play_sound_named(
                        facts, state->canon_audio, "Canon_a");
                }
            } else if (decision.committed == 1) {
                wz_log_infof(
                    facts,
                    "[tank] quantum mind committed: HOLD (z=%.2f)",
                    decision.marginal);
            } else {
                wz_log_infof(
                    facts,
                    "[tank] quantum mind deliberating (z=%.2f)",
                    decision.marginal);
            }
        }

        uint64_t frame_index =  wz_frame_index(facts);
        switch (wz_event_kind(event)) {

        case WZ_EVENT_SELF_START:
        {
            constexpr float kAlignRate = 2.094f;  // 120 deg/s
            wz_self_set_terrain_alignment_rate(facts, event, kAlignRate);
            return;   // set once; skip the motion code below on this event
        }

        case WZ_EVENT_INPUT_CONTROLLER_AXIS_CHANGED:
        {
            uint32_t controller = wz_input_event_controller(facts);
            uint32_t axis = wz_input_event_controller_axis(facts);
            float value = wz_input_event_controller_axis_value(facts);
           

            if (axis == 1) {
                state->left_tread_speed = value;
            }

            if (axis == 3) {
                state->right_tread_speed = value;
            }

            // wz_log_infof(facts, "frame %u axis %u controller %u value %.2f throttle %.2f turn %.2f",frame_index, axis, controller, value, state->throttle, state->turn);
            break;
        }
        case WZ_EVENT_INPUT_CONTROLLER_BUTTON_PRESSED:
        {
            uint32_t controller = wz_input_event_controller(facts);
            uint32_t button = wz_input_event_controller_button(facts);
            wz_log_infof(facts, "frame %u pressed controller %u button %u",frame_index, controller, button);
            
            if (button == 8) {
                wz_log_info(facts, "[tank] try fire canon");
                try_fire_canon(facts, event, state);
            }
            break;
        }
        case WZ_EVENT_INPUT_CONTROLLER_BUTTON_RELEASED:
        {
            uint32_t controller = wz_input_event_controller(facts);
            uint32_t button = wz_input_event_controller_button(facts);
            // wz_log_infof(facts, "farme %u released controller %u button %u",frame_index, controller, button);
            break;
        }
        
        
        default:
            break;
        }

        tank_drive::drive_treads(facts, event, state->left_tread_speed, state->right_tread_speed);

        // Engine grain synth: track tread effort -> rpm and crossfade the engine
        // cloud's program on a level change (runs on frame.update + input events).
        update_engine_rpm(facts, state);

    }
}

WZ_BEHAVIOR_MODULE_INIT(
    "tank_controller",
    tank_init,
    on_event,
    kTankEvents)
