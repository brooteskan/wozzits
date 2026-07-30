using System.Text.Json.Nodes;
using Wozzits.Editor.Statecharts;

namespace Wozzits.Editor.Tests.Statecharts;

/// <summary>Corpus-free unit tests for the model/compiler behaviours the oracle relies on.</summary>
public sealed class StatechartCompilerTests
{
    // A tiny well-formed chart: one binding, one owned agent, one read op, one
    // region/state that consumes the read.
    private static Chart MinimalValid()
    {
        var c = new Chart { Name = "t" };
        c.Bindings.Add(new Binding { Port = "lamp", Find = "lamp", Subtree = true });
        c.Agents.Add(new AgentDecl
        {
            Id = "sig",
            Owned = true,
            Host = "self",
            Spec = JsonNode.Parse("""{"decisions":1}"""),
        });
        c.Pure.Add(new PureOp { Id = "z", Op = OpKind.Marginal, Agent = "sig", Slot = 0 });
        var s = new State { Id = "S" };
        s.Do.Add(new Effect { Kind = EffectKind.SetScale, TargetBind = "lamp", Value = ValueRef.FromOp("z") });
        c.States.Add(s);
        var r = new Region { Id = "R", Initial = "S" };
        r.States.Add("S");
        c.Regions.Add(r);
        return c;
    }

    [Fact]
    public void MinimalChart_Validates_Clean()
    {
        Assert.Empty(StatechartJson.Validate(MinimalValid()));
    }

    [Fact]
    public void Compiler_Orders_Dependencies_Before_Uses()
    {
        var c = MinimalValid();
        c.Pure.Clear();
        c.Pure.Add(new PureOp { Id = "z", Op = OpKind.Marginal, Agent = "sig", Slot = 0 });
        var b = new PureOp { Id = "b", Op = OpKind.Clamp01 };
        b.Ins.Add(ValueRef.FromOp("a"));                 // b <- a
        c.Pure.Add(b);
        var a = new PureOp { Id = "a", Op = OpKind.Mul };
        a.Ins.Add(ValueRef.FromOp("z"));
        a.Ins.Add(ValueRef.Number(2));                   // a <- z * 2
        c.Pure.Add(a);
        // Authored order [z, b, a] is NOT topological (b precedes its input a).

        var pure = JsonNode.Parse(StatechartJson.Emit(c, indented: false))!["pure"]!.AsArray();
        var pos = new Dictionary<string, int>();
        for (int i = 0; i < pure.Count; i++) pos[pure[i]!["id"]!.GetValue<string>()] = i;

        Assert.True(pos["z"] < pos["a"], "z must precede a");
        Assert.True(pos["a"] < pos["b"], "a must precede b");
    }

    [Fact]
    public void AlreadySorted_Pure_ReEmits_In_Original_Order()
    {
        // Stable sort: an already-valid order must be preserved exactly.
        var c = MinimalValid();
        c.Pure.Add(new PureOp { Id = "c0", Op = OpKind.Committed, Agent = "sig", Slot = 0 });
        var m = new PureOp { Id = "m", Op = OpKind.MulAdd };
        m.Ins.Add(ValueRef.FromOp("z"));
        m.Ins.Add(ValueRef.Number(0.5));
        m.Ins.Add(ValueRef.Number(0.5));
        c.Pure.Add(m);   // order [z, c0, m] is valid

        var pure = JsonNode.Parse(StatechartJson.Emit(c, indented: false))!["pure"]!.AsArray();
        Assert.Equal(new[] { "z", "c0", "m" }, pure.Select(p => p!["id"]!.GetValue<string>()).ToArray());
    }

    [Fact]
    public void Validate_Flags_Dangling_Op_Ref()
    {
        var c = MinimalValid();
        var x = new PureOp { Id = "x", Op = OpKind.Clamp01 };
        x.Ins.Add(ValueRef.FromOp("nope"));
        c.Pure.Add(x);
        Assert.Contains(StatechartJson.Validate(c), e => e.Contains("unknown op 'nope'"));
    }

    [Fact]
    public void Validate_And_Emit_Reject_A_Cycle()
    {
        var c = MinimalValid();
        var a = new PureOp { Id = "a", Op = OpKind.Clamp01 }; a.Ins.Add(ValueRef.FromOp("b"));
        var b = new PureOp { Id = "b", Op = OpKind.Clamp01 }; b.Ins.Add(ValueRef.FromOp("a"));
        c.Pure.Add(a);
        c.Pure.Add(b);

        Assert.Contains(StatechartJson.Validate(c), e => e.Contains("cycle"));
        Assert.Throws<StatechartFormatException>(() => StatechartJson.Emit(c, indented: false));
    }

    [Fact]
    public void Validate_Flags_Write_To_NonOwned_Agent()   // R2: writes are owner-only
    {
        var c = MinimalValid();
        c.Agents.Add(new AgentDecl { Id = "peer", Owned = false, Host = "lamp" });
        c.States[0].Do.Add(new Effect { Kind = EffectKind.SetDecoherence, Agent = "peer", Value = ValueRef.Number(0.5) });
        Assert.Contains(StatechartJson.Validate(c), e => e.Contains("non-owned"));
    }

    [Fact]
    public void BoolConst_RoundTrips_As_Bool_Number_As_Number()
    {
        var c = MinimalValid();
        c.States[0].Do.Add(new Effect { Kind = EffectKind.SetVisible, TargetBind = "lamp", Value = ValueRef.Bool(true) });
        var m = new PureOp { Id = "m2", Op = OpKind.Mul };
        m.Ins.Add(ValueRef.FromOp("z"));
        m.Ins.Add(ValueRef.Number(3));
        c.Pure.Add(m);

        var back = StatechartJson.Load(StatechartJson.Emit(c, indented: false));
        var vis = back.States[0].Do.First(e => e.Kind == EffectKind.SetVisible);
        Assert.True(vis.Value!.IsBool);
        Assert.NotEqual(0.0, vis.Value.Const);

        var mul = back.Pure.First(p => p.Id == "m2");
        Assert.False(mul.Ins[1].IsBool);
        Assert.Equal(3.0, mul.Ins[1].Const);
    }

    [Fact]
    public void CommitOutcome_Any_And_Numeric_RoundTrip()
    {
        var c = MinimalValid();
        var any = new Transition { Target = "S", Trigger = new Trigger { Kind = TriggerKind.Commit, Agent = "sig", Slot = 0, Outcome = null } };
        var zero = new Transition { Target = "S", Trigger = new Trigger { Kind = TriggerKind.Commit, Agent = "sig", Slot = 0, Outcome = 0 } };
        c.States[0].Transitions.Add(any);
        c.States[0].Transitions.Add(zero);

        var ir = StatechartJson.Emit(c, indented: false);
        Assert.Contains("\"outcome\":\"any\"", ir);

        var back = StatechartJson.Load(ir);
        Assert.Null(back.States[0].Transitions[0].Trigger.Outcome);
        Assert.Equal(0, back.States[0].Transitions[1].Trigger.Outcome);
    }

    [Fact]
    public void Reward_Strength_And_Branch_RoundTrip()
    {
        var c = MinimalValid();
        var t = new Transition { Target = "S", Trigger = new Trigger { Kind = TriggerKind.Commit, Agent = "sig", Slot = 0, Outcome = 0 } };
        t.Actions.Add(new Effect { Kind = EffectKind.Reward, Agent = "sig", Slot = 0, Branch = true, Value = ValueRef.Number(0.5) });
        c.States[0].Transitions.Add(t);

        var back = StatechartJson.Load(StatechartJson.Emit(c, indented: false));
        var reward = back.States[0].Transitions[0].Actions[0];
        Assert.Equal(EffectKind.Reward, reward.Kind);
        Assert.True(reward.Branch);
        Assert.Equal(0, reward.Slot);
        Assert.Equal(0.5, reward.Value!.Const);
    }

    // The emitted key must be `branch`, and must NOT be the old `toward` -- the engine
    // parser refuses `toward` outright, so an editor that emitted it produced charts that
    // would not load at all.
    [Fact]
    public void Reward_Emits_Branch_Not_Toward()
    {
        var c = MinimalValid();
        var t = new Transition { Target = "S", Trigger = new Trigger { Kind = TriggerKind.Commit, Agent = "sig", Slot = 0, Outcome = 0 } };
        t.Actions.Add(new Effect { Kind = EffectKind.Reward, Agent = "sig", Slot = 0, Branch = true, Value = ValueRef.Number(0.5) });
        c.States[0].Transitions.Add(t);

        var json = StatechartJson.Emit(c, indented: false);

        Assert.Contains("\"branch\"", json);
        Assert.DoesNotContain("\"toward\"", json);
    }

    // A chart still carrying the pre-v38 key is REFUSED, not silently reinterpreted:
    // `toward` meant the opposite branch, so guessing would train backwards.
    [Fact]
    public void Reward_With_Legacy_Toward_Key_Is_Refused()
    {
        var c = MinimalValid();
        var t = new Transition { Target = "S", Trigger = new Trigger { Kind = TriggerKind.Commit, Agent = "sig", Slot = 0, Outcome = 0 } };
        t.Actions.Add(new Effect { Kind = EffectKind.Reward, Agent = "sig", Slot = 0, Branch = true, Value = ValueRef.Number(0.5) });
        c.States[0].Transitions.Add(t);
        var legacy = StatechartJson.Emit(c, indented: false).Replace("\"branch\"", "\"toward\"");

        var ex = Assert.Throws<StatechartFormatException>(() => StatechartJson.Load(legacy));
        Assert.Contains("branch", ex.Message);
    }

    [Fact]
    public void AgentRef_TargetName_RoundTrips_And_OwnedAgent_OmitsIt()   // v35 explicit refs
    {
        var c = MinimalValid();   // "sig": an OWNED agent (index 0)
        // A REF that NAMES which quantum_agent on the host it reads (index 1); and an
        // unnamed ref (index 2) that falls back to the first agent.
        c.Agents.Add(new AgentDecl { Id = "peer", Owned = false, Host = "self", AgentName = "beta" });
        c.Agents.Add(new AgentDecl { Id = "any", Owned = false, Host = "self" });

        var ir = StatechartJson.Emit(c, indented: false);
        var agents = JsonNode.Parse(ir)!["agents"]!.AsArray();
        Assert.Null(agents[0]!["agent"]);                       // owned -> no target
        Assert.Equal("beta", (string?)agents[1]!["agent"]);     // named ref -> label
        Assert.Null(agents[2]!["agent"]);                       // empty ref -> omitted

        var back = StatechartJson.Load(ir);
        Assert.Equal("beta", back.Agents.Single(a => a.Id == "peer").AgentName);
        Assert.Equal("", back.Agents.Single(a => a.Id == "any").AgentName);
        Assert.Equal("", back.Agents.Single(a => a.Id == "sig").AgentName);
    }

    [Fact]
    public void Memory_Emits_Q_Reads_Emit_Slot()
    {
        var c = MinimalValid();
        c.Pure.Add(new PureOp { Id = "m", Op = OpKind.Memory, Agent = "sig", Slot = 0 });

        var pure = JsonNode.Parse(StatechartJson.Emit(c, indented: false))!["pure"]!.AsArray();
        var mem = pure.First(p => p!["id"]!.GetValue<string>() == "m")!;
        Assert.NotNull(mem["q"]);
        Assert.Null(mem["slot"]);
        var read = pure.First(p => p!["id"]!.GetValue<string>() == "z")!;
        Assert.NotNull(read["slot"]);
        Assert.Null(read["q"]);
    }

    [Fact]
    public void EmptyEntryExit_Omitted_But_Transitions_Always_Present()
    {
        var state = JsonNode.Parse(StatechartJson.Emit(MinimalValid(), indented: false))!["states"]!.AsArray()[0]!;
        Assert.NotNull(state["do"]);
        Assert.NotNull(state["transitions"]);   // present even though empty
        Assert.Null(state["entry"]);
        Assert.Null(state["exit"]);
    }

    [Fact]
    public void Load_Rejects_Unknown_Op_Kind()
    {
        const string bad = """
            {"name":"x","bindings":[],"agents":[],
             "pure":[{"id":"a","op":"frobnicate"}],
             "regions":[{"id":"R","initial":"S","states":["S"]}],
             "states":[{"id":"S","transitions":[]}]}
            """;
        Assert.Throws<StatechartFormatException>(() => StatechartJson.Load(bad));
    }

    [Fact]
    public void Call_Effect_RoundTrips_All_Arg_Kinds()
    {
        // A call to a behavior-registered actuator with one arg of each kind:
        // a bound entity, a literal scalar, a pure-op output, and an agent host.
        var c = MinimalValid();
        var call = new Effect { Kind = EffectKind.Call, Fn = "move_toward" };
        call.Args.Add(CallArg.ToBind("lamp"));
        call.Args.Add(CallArg.Number(3.5));
        call.Args.Add(CallArg.FromOp("z"));
        call.Args.Add(CallArg.ToAgent("sig"));
        c.States[0].Do.Add(call);

        var ir = StatechartJson.Emit(c, indented: false);
        Assert.Contains("\"fn\":\"move_toward\"", ir);
        Assert.Contains("\"bind\":\"lamp\"", ir);
        Assert.Contains("\"agent\":\"sig\"", ir);

        var back = StatechartJson.Load(ir);
        var loaded = back.States[0].Do.First(e => e.Kind == EffectKind.Call);
        Assert.Equal("move_toward", loaded.Fn);
        Assert.Equal(4, loaded.Args.Count);
        Assert.Equal(CallArgKind.Bind, loaded.Args[0].Kind);
        Assert.Equal("lamp", loaded.Args[0].Bind);
        Assert.Equal(CallArgKind.Const, loaded.Args[1].Kind);
        Assert.Equal(3.5, loaded.Args[1].Const);
        Assert.Equal(CallArgKind.Op, loaded.Args[2].Kind);
        Assert.Equal("z", loaded.Args[2].Op);
        Assert.Equal(CallArgKind.Agent, loaded.Args[3].Kind);
        Assert.Equal("sig", loaded.Args[3].Agent);
    }

    [Fact]
    public void Call_Validates_Clean_And_Flags_Bad_Args()
    {
        var c = MinimalValid();
        var ok = new Effect { Kind = EffectKind.Call, Fn = "move_toward" };
        ok.Args.Add(CallArg.ToBind("lamp"));
        ok.Args.Add(CallArg.Number(3));
        c.States[0].Do.Add(ok);
        Assert.Empty(StatechartJson.Validate(c));

        var bad = new Effect { Kind = EffectKind.Call, Fn = "move_toward" };
        bad.Args.Add(CallArg.ToBind("nope"));
        bad.Args.Add(CallArg.ToAgent("ghost"));
        c.States[0].Do.Add(bad);
        var errors = StatechartJson.Validate(c);
        Assert.Contains(errors, e => e.Contains("unknown binding 'nope'"));
        Assert.Contains(errors, e => e.Contains("unknown agent 'ghost'"));
    }

    [Fact]
    public void Call_Missing_Fn_Is_Flagged()
    {
        var c = MinimalValid();
        c.States[0].Do.Add(new Effect { Kind = EffectKind.Call });   // no Fn
        Assert.Contains(StatechartJson.Validate(c), e => e.Contains("call missing fn"));
    }

    [Fact]
    public void EventTrigger_Name_RoundTrips()
    {
        // A behavior-event trigger (v34): the transition fires on a named event a
        // behavior emits. The name must survive emit -> load.
        var c = MinimalValid();
        c.States[0].Transitions.Add(new Transition
        {
            Target = "S",
            Trigger = new Trigger { Kind = TriggerKind.Event, EventName = "died" },
        });

        var ir = StatechartJson.Emit(c, indented: false);
        Assert.Contains("\"kind\":\"event\"", ir);
        Assert.Contains("\"name\":\"died\"", ir);

        var back = StatechartJson.Load(ir);
        var tr = back.States[0].Transitions.First(x => x.Trigger.Kind == TriggerKind.Event);
        Assert.Equal("died", tr.Trigger.EventName);
    }

    // EmitValidated is the gate every PERSISTING write goes through, so the editor cannot hand
    // the engine IR its parse_chart refuses. Before it existed, Validate had zero callers: the
    // editor would happily write a structurally broken chart to .sc.json and embed it as a
    // runner's chart_ir, and the only symptom was a runner that silently never started at play
    // time (parse_chart fails, the store returns null, the log says "bad IR" with no reason).
    //
    // Each case below is a class the ENGINE parser rejects, so each is a chart that would have
    // been written and then refused: no regions (statechart_ir.cpp), an unknown transition
    // target, an effect naming an agent that no longer exists.

    [Fact]
    public void EmitValidated_Refuses_A_Chart_With_No_Regions()
    {
        // The reachable route: deleting every state empties each region, and the control pane
        // then drops the empty regions -- leaving a chart the engine refuses outright.
        var c = MinimalValid();
        c.States.Clear();
        c.Regions.Clear();

        var ex = Assert.Throws<StatechartFormatException>(
            () => StatechartJson.EmitValidated(c, indented: true));
        Assert.Contains("no regions", ex.Message);
    }

    [Fact]
    public void EmitValidated_Refuses_A_Dangling_Transition_Target()
    {
        var c = MinimalValid();
        c.States[0].Transitions.Add(new Transition
        {
            Target = "DELETED",
            Trigger = new Trigger { Kind = TriggerKind.After, Seconds = 1.0 },
        });

        var ex = Assert.Throws<StatechartFormatException>(
            () => StatechartJson.EmitValidated(c, indented: false));
        Assert.Contains("DELETED", ex.Message);
    }

    [Fact]
    public void EmitValidated_Refuses_An_Effect_Naming_A_Missing_Agent()
    {
        var c = MinimalValid();
        c.States[0].Do.Add(new Effect
        {
            Kind = EffectKind.Rearm,
            Agent = "ghost",
        });

        var ex = Assert.Throws<StatechartFormatException>(
            () => StatechartJson.EmitValidated(c, indented: false));
        Assert.Contains("ghost", ex.Message);
    }

    [Fact]
    public void EmitValidated_Message_Names_The_Chart_And_Says_Nothing_Was_Written()
    {
        // Whatever surface shows this (the editor log, the runner-attach status line) has only
        // the message, so it has to identify WHICH chart and make clear the write did not happen.
        var c = MinimalValid();
        c.Name = "traffic_light";
        c.Regions.Clear();

        var ex = Assert.Throws<StatechartFormatException>(
            () => StatechartJson.EmitValidated(c, indented: true));
        Assert.Contains("traffic_light", ex.Message);
        Assert.Contains("not written", ex.Message);
    }

    [Fact]
    public void Load_Refuses_An_Unrecognized_Schema_But_Tolerates_A_Missing_One()
    {
        // A newer chart must not load as v0: this build would then re-Emit it under the v0
        // stamp and silently downgrade the file, losing whatever the newer revision meant.
        var newer = StatechartJson.Emit(MinimalValid(), indented: false)
            .Replace("wozzits.statechart.ir.v0", "wozzits.statechart.ir.v1");
        var ex = Assert.Throws<StatechartFormatException>(() => StatechartJson.Load(newer));
        Assert.Contains("wozzits.statechart.ir.v1", ex.Message);

        // A MISSING schema still loads (and Emit writes the field back, healing the file).
        // The editor is the tool you REPAIR a chart with; refusing to open one would leave
        // no route to fix it. The engine is strict here instead -- see parse_chart.
        var bare = JsonNode.Parse(StatechartJson.Emit(MinimalValid(), indented: false))!.AsObject();
        bare.Remove("schema");
        var loaded = StatechartJson.Load(bare.ToJsonString());
        Assert.Equal(StatechartSchema.V0, loaded.Schema);
        Assert.Contains(StatechartSchema.V0, StatechartJson.Emit(loaded, indented: false));
    }

    [Fact]
    public void Inspect_Classifies_The_R2_Owner_Only_Rule_As_Advisory()
    {
        // The engine PARSES an agent's `owned` flag and then never reads it -- neither
        // statechart_ir.cpp nor statechart_runner.cpp consults it -- so a write to a non-owned
        // agent loads and runs. R2 is an editor convention, so it must not be Blocking, or the
        // save gate refuses charts the engine is perfectly happy with.
        var c = MinimalValid();
        c.Agents.Add(new AgentDecl { Id = "peer", Owned = false, Host = "lamp" });
        c.States[0].Do.Add(new Effect
        {
            Kind = EffectKind.SetDecoherence,
            Agent = "peer",
            Value = ValueRef.Number(0.5),
        });

        var issues = StatechartJson.Inspect(c);
        var r2 = Assert.Single(issues, i => i.Message.Contains("non-owned"));
        Assert.Equal(StatechartJson.IssueSeverity.Advisory, r2.Severity);
        Assert.DoesNotContain(issues, i => i.Severity == StatechartJson.IssueSeverity.Blocking);
    }

    [Fact]
    public void EmitValidated_Allows_A_Chart_Whose_Only_Findings_Are_Advisory()
    {
        // The guard against over-blocking. hunt_or_refuel.sc.json -- a chart that ships in the
        // project and runs -- trips the R2 rule 8 times; gating on every finding would have made
        // it, and any chart like it, permanently unsaveable. An advisory chart must still emit.
        var c = MinimalValid();
        c.Agents.Add(new AgentDecl { Id = "peer", Owned = false, Host = "lamp" });
        c.States[0].Do.Add(new Effect { Kind = EffectKind.Rearm, Agent = "peer" });

        Assert.NotEmpty(StatechartJson.Validate(c));                       // reported...
        Assert.Contains("\"id\":\"peer\"", StatechartJson.EmitValidated(c, indented: false));
    }                                                                      // ...but written

    [Fact]
    public void EmitValidated_Passes_A_Valid_Chart_Through_Byte_Identically()
    {
        // Validation gates, it never rewrites: a good chart must serialize exactly as Emit does,
        // or turning the gate on would churn every .sc.json in the project on next save.
        var c = MinimalValid();

        Assert.Equal(
            StatechartJson.Emit(c, indented: true),
            StatechartJson.EmitValidated(c, indented: true));
        Assert.Equal(
            StatechartJson.Emit(c, indented: false),
            StatechartJson.EmitValidated(c, indented: false));
    }
}
