#include <engine/behavior/behavior_module_api.h>

// skinner -- a one-qubit LEARNING demo (operant conditioning). Each cycle the
// co-located quantum_agent commits |0> (go LEFT) or |1> (go RIGHT). LEFT always pays:
// on a |0> commit it rewards the memory toward |0>; on a |1> commit it punishes |1>
// (right didn't pay) -- both push the memory toward "left". Then it feeds the learned
// bias forward as the next cycle's goal (goal = memory_preference * gain), so what it
// learned LITERALLY becomes next cycle's bias. Over ~5-10 cycles the choice drifts from
// ~50/50 to almost-always-left, and as the goal grows the commits get FASTER (two
// independent learning signals: which way it goes, and how fast it decides). The memory
// lives OUTSIDE the coordination, so it survives every re-arm.
//
// Config (optional): reward [0.5] left-pays strength, punish [0.3] right strength,
// gain [0.8] memory->goal, period [6] seconds committed before the next cycle,
// memory_qubit [0].

namespace
{
    struct SkinnerState
    {
        int8_t last_committed = -2;  // commit-edge detection (-2 uninit)
        float timer = 0.0f;          // seconds committed, for the cycle metronome
        uint32_t cycle = 0;
    };

    static const char* kSkinnerEvents[] = { "self.start", "frame.update" };

    void skinner_init(const WzBehaviorInitFacts* facts, WzBehaviorEntityId, void*)
    {
        (void)wz_instance_state<SkinnerState>(facts);
    }

    void skinner_on_event(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        void*)
    {
        if (!facts || !event) {
            return;
        }
        const WzBehaviorEventKind kind = wz_event_kind(event);
        SkinnerState* s = wz_instance_state<SkinnerState>(facts);
        if (!s) {
            return;
        }
        if (kind == WZ_EVENT_SELF_START) {
            s->last_committed = -2;
            s->timer = 0.0f;
            s->cycle = 0;
            return;
        }
        if (kind != WZ_EVENT_FRAME_UPDATE) {
            return;
        }

        double reward = 0.5, punish = 0.3, gain = 0.8, period = 6.0, mq = 0.0;
        wz_config_number(facts, "reward", &reward);
        wz_config_number(facts, "punish", &punish);
        wz_config_number(facts, "gain", &gain);
        wz_config_number(facts, "period", &period);
        wz_config_number(facts, "memory_qubit", &mq);
        const uint32_t memq = mq < 0.0 ? 0u : static_cast<uint32_t>(mq);

        WzAgentDecision d{};
        if (!wz_self_agent_decision(facts, event, &d)) {
            return;  // no co-located quantum_agent (or no memory)
        }

        // Commit EDGE: reward the outcome, then feed the learned bias forward as the
        // decision's goal for the next cycle (goals survive the re-arm below).
        if (d.committed >= 0 && s->last_committed < 0) {
            s->cycle++;
            const bool left = (d.committed == 0);
            if (left) {
                // LEFT paid -> reinforce the 0 branch. (v38 VALUE semantics: the
                // flag names the branch, so 0 means the 0 branch. This used to
                // read `toward = 1` for the same thing.)
                wz_self_agent_reward(
                    facts, event, memq, /*value=*/ 0u,
                    static_cast<float>(reward));
            }
            else {
                // RIGHT did not pay -> punish the 1 branch (leans memory to 0).
                wz_self_agent_reward(
                    facts, event, memq, /*value=*/ 1u,
                    -static_cast<float>(punish));
            }
            const float mem = wz_self_agent_memory(facts, event, memq);
            wz_self_set_agent_goal(
                facts, event, 0u, mem * static_cast<float>(gain));
            wz_log_infof(
                facts,
                "[skinner] cycle %u chose %s  memory=%+.2f  next_goal=%+.2f",
                s->cycle, left ? "LEFT(pays)" : "right",
                static_cast<double>(mem),
                static_cast<double>(mem * static_cast<float>(gain)));
            s->timer = 0.0f;
        }

        // Hold the committed choice `period` seconds, then re-open for the next trial.
        if (d.committed >= 0) {
            s->timer += wz_delta_seconds(facts);
            if (period > 0.0 && static_cast<double>(s->timer) >= period) {
                wz_self_rearm_agent(facts, event);
                s->timer = 0.0f;
            }
        }
        else {
            s->timer = 0.0f;
        }

        s->last_committed = d.committed;
    }
}

WZ_BEHAVIOR_MODULE_INIT(
    "skinner",
    skinner_init,
    skinner_on_event,
    kSkinnerEvents)
