using Wozzits.Editor.Protocol;
using Wozzits.Editor.Statecharts;
using Wozzits.Editor.ViewModels.EditorPanes.Statecharts;

namespace Wozzits.Editor.Tests.Statecharts;

// Seam 2c: a `call` effect row drives its actuator picker + per-arg pickers off the
// device-free actuator catalog, and keeps the underlying Effect.Args shaped to the
// chosen actuator's schema so the emitted IR is valid. These are VM-level tests (no
// Avalonia rendering) of that wiring.
public sealed class StatechartCallEffectTests
{
    private static List<EngineActuator> MoveTowardCatalog() =>
    [
        new()
        {
            Name = "move_toward",
            Label = "Move toward",
            Params =
            [
                new EngineActuatorParam { Name = "target", Kind = 1, DefaultValue = 0 },  // binding
                new EngineActuatorParam { Name = "speed", Kind = 0, DefaultValue = 1 },    // scalar
            ],
        },
    ];

    private static EffectRowViewModel NewCallRow(Effect effect) =>
        new(effect, null)
        {
            TargetBindings = new[] { "prey", "home" },
            ValueSources = new[] { EffectRowViewModel.ConstSentinel, "z" },
            AvailableAgents = new[] { "sig" },
            ActuatorCatalog = MoveTowardCatalog(),   // last, mirroring the pane's push order
        };

    [Fact]
    public void Call_Row_Defaults_Fn_And_Seeds_Args_From_Schema()
    {
        var effect = new Effect { Kind = EffectKind.Call };
        var row = NewCallRow(effect);

        Assert.True(row.IsCall);
        Assert.Contains("move_toward", row.ActuatorNames);
        Assert.Equal("move_toward", row.SelectedActuator);   // defaulted to the first actuator
        Assert.Equal(2, row.CallArgs.Count);

        var target = row.CallArgs[0];
        Assert.Equal("target", target.ParamName);
        Assert.True(target.IsBinding);
        Assert.Equal("prey", target.SelectedBinding);        // seeded to the first binding

        var speed = row.CallArgs[1];
        Assert.Equal("speed", speed.ParamName);
        Assert.True(speed.IsScalar);
        Assert.True(speed.IsEditable);

        // The model is shaped for emission.
        Assert.Equal("move_toward", effect.Fn);
        Assert.Equal(2, effect.Args.Count);
        Assert.Equal(CallArgKind.Bind, effect.Args[0].Kind);
        Assert.Equal("prey", effect.Args[0].Bind);
        Assert.Equal(CallArgKind.Const, effect.Args[1].Kind);
        Assert.Equal(1.0, effect.Args[1].Const);
    }

    [Fact]
    public void Editing_Arg_Pickers_Updates_The_Model()
    {
        var effect = new Effect { Kind = EffectKind.Call };
        var row = NewCallRow(effect);

        row.CallArgs[0].SelectedBinding = "home";
        Assert.Equal("home", effect.Args[0].Bind);

        // Flip the scalar arg from a constant to a pure-op output.
        row.CallArgs[1].SelectedValueSource = "z";
        Assert.Equal(CallArgKind.Op, effect.Args[1].Kind);
        Assert.Equal("z", effect.Args[1].Op);
        Assert.False(row.CallArgs[1].IsEditable);   // op-valued -> no constant editor
    }

    [Fact]
    public void Call_Effect_Authored_Through_The_Row_Emits_Valid_Ir()
    {
        // A minimal chart with a binding + agent so the emitted call validates clean.
        var c = new Chart { Name = "t" };
        c.Bindings.Add(new Binding { Port = "prey", Find = "prey" });
        c.Agents.Add(new AgentDecl { Id = "sig", Owned = true, Host = "self" });
        var s = new State { Id = "S" };
        var effect = new Effect { Kind = EffectKind.Call };
        s.Do.Add(effect);
        c.States.Add(s);
        var r = new Region { Id = "R", Initial = "S" };
        r.States.Add("S");
        c.Regions.Add(r);

        // Author it as the inspector would: seed from the catalog, then pick a binding.
        var row = new EffectRowViewModel(effect, null)
        {
            TargetBindings = new[] { "prey" },
            ValueSources = new[] { EffectRowViewModel.ConstSentinel },
            AvailableAgents = new[] { "sig" },
            ActuatorCatalog = MoveTowardCatalog(),
        };
        row.CallArgs[1].ValueEditor!.Value = "3.5";   // speed = 3.5

        Assert.Empty(StatechartJson.Validate(c));
        var ir = StatechartJson.Emit(c, indented: false);
        Assert.Contains("\"fn\":\"move_toward\"", ir);
        Assert.Contains("\"bind\":\"prey\"", ir);
        Assert.Contains("3.5", ir);

        // Round-trips back to the same shape.
        var back = StatechartJson.Load(ir);
        var call = back.States[0].Do.Single(e => e.Kind == EffectKind.Call);
        Assert.Equal("move_toward", call.Fn);
        Assert.Equal(CallArgKind.Bind, call.Args[0].Kind);
        Assert.Equal(3.5, call.Args[1].Const);
    }
}
