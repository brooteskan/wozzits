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
    public void Reward_Strength_And_Toward_RoundTrip()
    {
        var c = MinimalValid();
        var t = new Transition { Target = "S", Trigger = new Trigger { Kind = TriggerKind.Commit, Agent = "sig", Slot = 0, Outcome = 0 } };
        t.Actions.Add(new Effect { Kind = EffectKind.Reward, Agent = "sig", Slot = 0, Toward = true, Value = ValueRef.Number(0.5) });
        c.States[0].Transitions.Add(t);

        var back = StatechartJson.Load(StatechartJson.Emit(c, indented: false));
        var reward = back.States[0].Transitions[0].Actions[0];
        Assert.Equal(EffectKind.Reward, reward.Kind);
        Assert.True(reward.Toward);
        Assert.Equal(0, reward.Slot);
        Assert.Equal(0.5, reward.Value!.Const);
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
}
