using System.Text.Json;
using System.Text.Json.Nodes;

namespace Wozzits.Editor.Statecharts;

/// <summary>Thrown when statechart JSON is malformed or structurally invalid.</summary>
public sealed class StatechartFormatException : Exception
{
    public StatechartFormatException(string message) : base(message) { }
}

/// <summary>
/// Loads and compiles a cognition-behavior statechart between the id-based
/// on-disk schema (`wozzits.statechart.ir.v0`) and the in-memory authoring model.
///
/// The on-disk form is the SAME id-based JSON the engine's parse_chart reads --
/// it resolves ids -> indices at load; we never emit indices. Compile =
/// topologically order the pure ops (the engine resolves op-refs incrementally,
/// so a definition must precede its uses) + serialize. <c>Emit(indented:false)</c>
/// is the <c>chart_ir</c> string embedded in a scene behavior; <c>Emit(indented:true)</c>
/// is the readable <c>.sc.json</c> source. A stable topological sort (tie-broken by
/// authored order) means an already-sorted chart re-emits in its original order.
/// </summary>
public static class StatechartJson
{
    // ---- name tables -----------------------------------------------------

    private static readonly (string Name, OpKind Kind)[] OpTable =
    {
        ("marginal", OpKind.Marginal), ("committed", OpKind.Committed), ("memory", OpKind.Memory),
        ("add", OpKind.Add), ("sub", OpKind.Sub), ("mul", OpKind.Mul), ("min", OpKind.Min),
        ("max", OpKind.Max), ("mul_add", OpKind.MulAdd), ("clamp01", OpKind.Clamp01),
        ("eq", OpKind.Eq), ("lt", OpKind.Lt), ("gt", OpKind.Gt), ("and", OpKind.And),
        ("or", OpKind.Or), ("not", OpKind.Not), ("select", OpKind.Select),
        ("proximity", OpKind.Proximity),
    };

    private static readonly (string Name, EffectKind Kind)[] EffectTable =
    {
        ("set_goal", EffectKind.SetGoal), ("set_decoherence", EffectKind.SetDecoherence),
        ("rearm", EffectKind.Rearm), ("reward", EffectKind.Reward),
        ("measure_at", EffectKind.MeasureAt),
        ("set_scale", EffectKind.SetScale), ("set_visible", EffectKind.SetVisible),
        ("play_sound", EffectKind.PlaySound), ("call", EffectKind.Call),
    };

    private static readonly (string Name, TriggerKind Kind)[] TriggerTable =
    {
        ("commit", TriggerKind.Commit), ("after", TriggerKind.After),
        ("guard", TriggerKind.Guard), ("event", TriggerKind.Event),
    };

    private static string NameOf(OpKind k) => OpTable.First(t => t.Kind == k).Name;
    private static string NameOf(EffectKind k) => EffectTable.First(t => t.Kind == k).Name;
    private static string NameOf(TriggerKind k) => TriggerTable.First(t => t.Kind == k).Name;

    // ======================================================================
    //  Load: .sc.json / chart_ir text -> authoring model
    // ======================================================================

    public static Chart Load(string json)
    {
        JsonNode? root;
        try
        {
            root = JsonNode.Parse(json);
        }
        catch (JsonException e)
        {
            throw new StatechartFormatException($"JSON parse failed: {e.Message}");
        }
        if (root is not JsonObject o)
            throw new StatechartFormatException("chart root must be an object");

        var c = new Chart
        {
            Schema = Str(o, "schema", StatechartSchema.V0),
            Name = Str(o, "name"),
        };

        foreach (var b in Arr(o, "bindings"))
            c.Bindings.Add(new Binding
            {
                Port = Str(b, "port"),
                Find = Str(b, "find"),
                Subtree = Str(b, "scope", "subtree") != "global",
            });

        foreach (var a in Arr(o, "agents"))
        {
            var host = Str(a, "host", "self");
            c.Agents.Add(new AgentDecl
            {
                Id = Str(a, "id"),
                Owned = Bool(a, "owned", true),
                Host = host.Length == 0 ? "self" : host,
                AgentName = Str(a, "agent"),
                Mind = Str(a, "mind"),
                Spec = (a as JsonObject)?["spec"]?.DeepClone(),
            });
        }

        foreach (var p in Arr(o, "pure"))
            c.Pure.Add(LoadPure(p));

        foreach (var s in Arr(o, "states"))
            c.States.Add(LoadState(s));

        foreach (var r in Arr(o, "regions"))
        {
            var rg = new Region { Id = Str(r, "id"), Initial = Str(r, "initial") };
            foreach (var sid in ArrVals(r, "states"))
                if (sid is JsonValue jv && jv.TryGetValue<string>(out var s))
                    rg.States.Add(s);
            c.Regions.Add(rg);
        }

        return c;
    }

    private static PureOp LoadPure(JsonNode p)
    {
        var kind = Str(p, "op");
        if (!OpTable.Any(t => t.Name == kind))
            throw new StatechartFormatException($"unknown pure op kind '{kind}'");
        var op = new PureOp { Id = Str(p, "id"), Op = OpTable.First(t => t.Name == kind).Kind };
        if (op.Id.Length == 0)
            throw new StatechartFormatException("pure op missing id");

        switch (op.Op)
        {
            case OpKind.Marginal:
            case OpKind.Committed:
                op.Agent = Str(p, "agent");
                op.Slot = (int)Num(p, "slot");
                break;
            case OpKind.Memory:
                op.Agent = Str(p, "agent");
                op.Slot = (int)Num(p, "q");
                break;
            case OpKind.Proximity:
                op.Target = Str(p, "target");
                break;
            case OpKind.Select:
                op.Cond = LoadRef(Member(p, "cond"));
                op.A = LoadRef(Member(p, "a"));
                op.B = LoadRef(Member(p, "b"));
                break;
            default:
                foreach (var r in ArrVals(p, "ins"))
                    op.Ins.Add(LoadRef(r));
                break;
        }
        return op;
    }

    private static State LoadState(JsonNode s)
    {
        var st = new State { Id = Str(s, "id") };
        foreach (var e in Arr(s, "do")) st.Do.Add(LoadEffect(e));
        foreach (var e in Arr(s, "entry")) st.Entry.Add(LoadEffect(e));
        foreach (var e in Arr(s, "exit")) st.Exit.Add(LoadEffect(e));
        foreach (var t in Arr(s, "transitions"))
        {
            var tr = new Transition
            {
                Trigger = LoadTrigger(Member(t, "trigger")),
                Target = Str(t, "target"),
            };
            foreach (var e in Arr(t, "actions")) tr.Actions.Add(LoadEffect(e));
            st.Transitions.Add(tr);
        }
        return st;
    }

    private static Trigger LoadTrigger(JsonNode t)
    {
        var tr = new Trigger();
        var kind = Str(t, "kind");
        switch (kind)
        {
            case "commit":
                tr.Kind = TriggerKind.Commit;
                tr.Agent = Str(t, "agent");
                tr.Slot = (int)Num(t, "slot");
                tr.Outcome = (t is JsonObject o && o.TryGetPropertyValue("outcome", out var v)
                              && v is JsonValue jv && jv.TryGetValue<double>(out var d))
                    ? (int)d
                    : null;   // "any" or absent
                break;
            case "after":
                tr.Kind = TriggerKind.After;
                tr.Seconds = Num(t, "seconds");
                break;
            case "guard":
                tr.Kind = TriggerKind.Guard;
                tr.Cond = LoadRef(Member(t, "cond"));
                break;
            case "event":
                tr.Kind = TriggerKind.Event;
                tr.EventName = Str(t, "name");
                break;
            default:
                throw new StatechartFormatException($"unknown trigger kind '{kind}'");
        }
        return tr;
    }

    private static Effect LoadEffect(JsonNode e)
    {
        var kind = Str(e, "kind");
        if (!EffectTable.Any(t => t.Name == kind))
            throw new StatechartFormatException($"unknown effect kind '{kind}'");
        var ef = new Effect { Kind = EffectTable.First(t => t.Name == kind).Kind };

        switch (ef.Kind)
        {
            case EffectKind.SetGoal:
            case EffectKind.MeasureAt:
                ef.Agent = Str(e, "agent");
                ef.Slot = (int)Num(e, "slot");
                ef.Value = LoadRef(Member(e, "value"));
                break;
            case EffectKind.SetDecoherence:
                ef.Agent = Str(e, "agent");
                ef.Value = LoadRef(Member(e, "value"));
                break;
            case EffectKind.Rearm:
                ef.Agent = Str(e, "agent");
                break;
            case EffectKind.Reward:
                ef.Agent = Str(e, "agent");
                ef.Slot = (int)Num(e, "q");
                ef.Toward = Bool(e, "toward");
                if (e is JsonObject o && o.ContainsKey("strength"))
                    ef.Value = LoadRef(Member(e, "strength"));
                break;
            case EffectKind.SetScale:
            case EffectKind.SetVisible:
                ef.TargetBind = TargetBindOf(e);
                ef.Value = LoadRef(Member(e, "value"));
                break;
            case EffectKind.PlaySound:
                ef.TargetBind = TargetBindOf(e);
                break;
            case EffectKind.Call:
                ef.Fn = Str(e, "fn");
                if (Member(e, "args") is JsonArray args)
                    foreach (var a in args)
                        if (a != null) ef.Args.Add(LoadCallArg(a));
                break;
        }
        return ef;
    }

    private static ValueRef LoadRef(JsonNode? n)
    {
        if (n is not JsonObject o)
            throw new StatechartFormatException("ref must be an object");
        if (o.TryGetPropertyValue("const", out var cv) && cv is JsonValue cjv)
        {
            if (cjv.TryGetValue<bool>(out var b)) return ValueRef.Bool(b);
            if (cjv.TryGetValue<double>(out var d)) return ValueRef.Number(d);
            throw new StatechartFormatException("const must be a number or bool");
        }
        if (o.TryGetPropertyValue("op", out var ov) && ov is JsonValue ojv && ojv.TryGetValue<string>(out var id))
            return ValueRef.FromOp(id);
        throw new StatechartFormatException("ref needs 'const' or 'op'");
    }

    private static CallArg LoadCallArg(JsonNode n)
    {
        if (n is not JsonObject o)
            throw new StatechartFormatException("call arg must be an object");
        if (o.TryGetPropertyValue("bind", out var bv) && bv is JsonValue bjv && bjv.TryGetValue<string>(out var port))
            return CallArg.ToBind(port);
        if (o.TryGetPropertyValue("agent", out var av) && av is JsonValue ajv && ajv.TryGetValue<string>(out var agent))
            return CallArg.ToAgent(agent);
        if (o.TryGetPropertyValue("op", out var ov) && ov is JsonValue ojv && ojv.TryGetValue<string>(out var id))
            return CallArg.FromOp(id);
        if (o.TryGetPropertyValue("const", out var cv) && cv is JsonValue cjv)
        {
            if (cjv.TryGetValue<bool>(out var b)) return new CallArg { Kind = CallArgKind.Const, Const = b ? 1 : 0, IsBool = true };
            if (cjv.TryGetValue<double>(out var d)) return CallArg.Number(d);
            throw new StatechartFormatException("call arg const must be a number or bool");
        }
        throw new StatechartFormatException("call arg needs 'bind', 'agent', 'op', or 'const'");
    }

    // ======================================================================
    //  Compile: authoring model -> .sc.json / chart_ir text
    // ======================================================================

    /// <summary>Compile the chart to JSON text. <paramref name="indented"/> selects the
    /// readable .sc.json form vs the minified chart_ir embed.</summary>
    public static string Emit(Chart c, bool indented) =>
        ToJson(c).ToJsonString(new JsonSerializerOptions { WriteIndented = indented });

    /// <summary>Compile the chart to a JSON tree (pure[] topologically ordered).</summary>
    public static JsonObject ToJson(Chart c) => new()
    {
        ["schema"] = c.Schema,
        ["name"] = c.Name,
        ["bindings"] = JArr(c.Bindings, EmitBinding),
        ["agents"] = JArr(c.Agents, EmitAgent),
        ["pure"] = JArr(TopoSortPure(c), EmitPure),
        ["regions"] = JArr(c.Regions, EmitRegion),
        ["states"] = JArr(c.States, EmitState),
    };

    private static JsonObject EmitBinding(Binding b) => new()
    {
        ["port"] = b.Port,
        ["find"] = b.Find,
        ["scope"] = b.Subtree ? "subtree" : "global",
    };

    private static JsonObject EmitAgent(AgentDecl a)
    {
        var o = new JsonObject { ["id"] = a.Id, ["owned"] = a.Owned, ["host"] = a.Host };
        // Only a REF names its target agent + mind; an owned agent creates its own from Spec.
        if (!a.Owned && a.AgentName.Length > 0) o["agent"] = a.AgentName;
        if (!a.Owned && a.Mind.Length > 0) o["mind"] = a.Mind;
        if (a.Spec != null) o["spec"] = a.Spec.DeepClone();
        return o;
    }

    private static JsonObject EmitPure(PureOp p)
    {
        var o = new JsonObject { ["id"] = p.Id, ["op"] = NameOf(p.Op) };
        switch (p.Op)
        {
            case OpKind.Marginal:
            case OpKind.Committed:
                o["agent"] = p.Agent;
                o["slot"] = p.Slot;
                break;
            case OpKind.Memory:
                o["agent"] = p.Agent;
                o["q"] = p.Slot;
                break;
            case OpKind.Proximity:
                o["target"] = p.Target;
                break;
            case OpKind.Select:
                o["cond"] = EmitRef(p.Cond);
                o["a"] = EmitRef(p.A);
                o["b"] = EmitRef(p.B);
                break;
            default:
                o["ins"] = JArr(p.Ins, EmitRef);
                break;
        }
        return o;
    }

    private static JsonObject EmitRegion(Region r) => new()
    {
        ["id"] = r.Id,
        ["initial"] = r.Initial,
        ["states"] = new JsonArray(r.States.Select(s => (JsonNode?)JsonValue.Create(s)).ToArray()),
    };

    private static JsonObject EmitState(State s)
    {
        var o = new JsonObject { ["id"] = s.Id };
        if (s.Do.Count > 0) o["do"] = JArr(s.Do, EmitEffect);
        if (s.Entry.Count > 0) o["entry"] = JArr(s.Entry, EmitEffect);
        if (s.Exit.Count > 0) o["exit"] = JArr(s.Exit, EmitEffect);
        o["transitions"] = JArr(s.Transitions, EmitTransition);
        return o;
    }

    private static JsonObject EmitTransition(Transition t)
    {
        var o = new JsonObject { ["trigger"] = EmitTrigger(t.Trigger) };
        if (t.Actions.Count > 0) o["actions"] = JArr(t.Actions, EmitEffect);
        o["target"] = t.Target;
        return o;
    }

    private static JsonObject EmitTrigger(Trigger t)
    {
        var o = new JsonObject { ["kind"] = NameOf(t.Kind) };
        switch (t.Kind)
        {
            case TriggerKind.Commit:
                o["agent"] = t.Agent;
                o["slot"] = t.Slot;
                o["outcome"] = t.Outcome is int oc ? JsonValue.Create(oc) : JsonValue.Create("any");
                break;
            case TriggerKind.After:
                o["seconds"] = t.Seconds;
                break;
            case TriggerKind.Guard:
                o["cond"] = EmitRef(t.Cond);
                break;
            case TriggerKind.Event:
                o["name"] = t.EventName;
                break;
        }
        return o;
    }

    private static JsonObject EmitEffect(Effect e)
    {
        var o = new JsonObject { ["kind"] = NameOf(e.Kind) };
        switch (e.Kind)
        {
            case EffectKind.SetGoal:
            case EffectKind.MeasureAt:
                o["agent"] = e.Agent;
                o["slot"] = e.Slot;
                o["value"] = EmitRef(e.Value);
                break;
            case EffectKind.SetDecoherence:
                o["agent"] = e.Agent;
                o["value"] = EmitRef(e.Value);
                break;
            case EffectKind.Rearm:
                o["agent"] = e.Agent;
                break;
            case EffectKind.Reward:
                o["agent"] = e.Agent;
                o["q"] = e.Slot;
                o["toward"] = e.Toward;
                if (e.Value != null) o["strength"] = EmitRef(e.Value);
                break;
            case EffectKind.SetScale:
            case EffectKind.SetVisible:
                o["target"] = new JsonObject { ["bind"] = e.TargetBind };
                o["value"] = EmitRef(e.Value);
                break;
            case EffectKind.PlaySound:
                o["target"] = new JsonObject { ["bind"] = e.TargetBind };
                break;
            case EffectKind.Call:
                o["fn"] = e.Fn;
                o["args"] = JArr(e.Args, EmitCallArg);
                break;
        }
        return o;
    }

    private static JsonNode EmitRef(ValueRef? r)
    {
        if (r == null)
            throw new StatechartFormatException("missing value ref");
        return r.Kind == RefKind.Op
            ? new JsonObject { ["op"] = r.Op }
            : new JsonObject { ["const"] = r.IsBool ? JsonValue.Create(r.Const != 0) : JsonValue.Create(r.Const) };
    }

    private static JsonNode EmitCallArg(CallArg a) => a.Kind switch
    {
        CallArgKind.Op => new JsonObject { ["op"] = a.Op },
        CallArgKind.Bind => new JsonObject { ["bind"] = a.Bind },
        CallArgKind.Agent => new JsonObject { ["agent"] = a.Agent },
        _ => new JsonObject { ["const"] = a.IsBool ? JsonValue.Create(a.Const != 0) : JsonValue.Create(a.Const) },
    };

    // ======================================================================
    //  Topological sort of pure ops (stable: tie-broken by authored order)
    // ======================================================================

    private static List<PureOp> TopoSortPure(Chart c)
    {
        int n = c.Pure.Count;
        var index = new Dictionary<string, int>(n);
        for (int i = 0; i < n; i++)
        {
            if (c.Pure[i].Id.Length == 0)
                throw new StatechartFormatException("pure op missing id");
            if (!index.TryAdd(c.Pure[i].Id, i))
                throw new StatechartFormatException($"duplicate pure op id '{c.Pure[i].Id}'");
        }

        var indegree = new int[n];
        var consumers = new List<int>[n];
        for (int i = 0; i < n; i++) consumers[i] = new List<int>();
        for (int i = 0; i < n; i++)
            foreach (var dep in OpRefs(c.Pure[i]))
            {
                if (!index.TryGetValue(dep, out var j))
                    throw new StatechartFormatException($"pure op '{c.Pure[i].Id}' references unknown op '{dep}'");
                consumers[j].Add(i);
                indegree[i]++;
            }

        // SortedSet.Min => always pick the lowest authored index among ready ops,
        // so a chart already in a valid topological order re-emits unchanged.
        var ready = new SortedSet<int>();
        for (int i = 0; i < n; i++)
            if (indegree[i] == 0) ready.Add(i);

        var order = new List<PureOp>(n);
        while (ready.Count > 0)
        {
            int i = ready.Min;
            ready.Remove(i);
            order.Add(c.Pure[i]);
            foreach (var k in consumers[i])
                if (--indegree[k] == 0) ready.Add(k);
        }

        if (order.Count != n)
            throw new StatechartFormatException("pure ops contain a cycle");
        return order;
    }

    private static IEnumerable<string> OpRefs(PureOp p)
    {
        foreach (var r in p.Ins)
            if (r.Kind == RefKind.Op) yield return r.Op;
        if (p.Cond?.Kind == RefKind.Op) yield return p.Cond.Op;
        if (p.A?.Kind == RefKind.Op) yield return p.A.Op;
        if (p.B?.Kind == RefKind.Op) yield return p.B.Op;
    }

    // ======================================================================
    //  Validate: referential integrity for the editor's error surface
    // ======================================================================

    /// <summary>Structural / referential problems (empty for a well-formed chart).
    /// The editor surfaces these; the engine's parse_chart rejects the same classes.</summary>
    public static IReadOnlyList<string> Validate(Chart c)
    {
        var errors = new List<string>();

        var bindingPorts = new HashSet<string>();
        foreach (var b in c.Bindings)
            if (!bindingPorts.Add(b.Port))
                errors.Add($"duplicate binding port '{b.Port}'");

        var agentIds = new HashSet<string>();
        var ownedAgents = new HashSet<string>();
        foreach (var a in c.Agents)
        {
            if (!agentIds.Add(a.Id)) errors.Add($"duplicate agent id '{a.Id}'");
            if (a.Owned) ownedAgents.Add(a.Id);
            if (a.Host != "self" && !bindingPorts.Contains(a.Host))
                errors.Add($"agent '{a.Id}' host '{a.Host}' is not a binding port");
        }

        var pureIds = new HashSet<string>();
        foreach (var p in c.Pure)
        {
            if (p.Id.Length == 0) { errors.Add("pure op missing id"); continue; }
            if (!pureIds.Add(p.Id)) errors.Add($"duplicate pure op id '{p.Id}'");
        }
        foreach (var p in c.Pure)
        {
            if (p.IsRead && !agentIds.Contains(p.Agent))
                errors.Add($"pure op '{p.Id}' reads unknown agent '{p.Agent}'");
            if (p.Op == OpKind.Proximity && !bindingPorts.Contains(p.Target))
                errors.Add($"proximity '{p.Id}' names unknown target binding '{p.Target}'");
            foreach (var dep in OpRefs(p))
                if (!pureIds.Contains(dep))
                    errors.Add($"pure op '{p.Id}' references unknown op '{dep}'");
        }
        if (HasPureCycle(c))
            errors.Add("pure ops contain a cycle");

        var stateIds = new HashSet<string>();
        foreach (var s in c.States)
            if (!stateIds.Add(s.Id))
                errors.Add($"duplicate state id '{s.Id}'");

        void CheckRef(ValueRef? r, string where)
        {
            if (r is { Kind: RefKind.Op } && !pureIds.Contains(r.Op))
                errors.Add($"{where} references unknown op '{r.Op}'");
        }
        void CheckEffect(Effect e, string where)
        {
            switch (e.Kind)
            {
                case EffectKind.MeasureAt:
                    // A measurement reads-with-collapse; unlike the R2 writes it may
                    // target a REFERENCED mind (the chart measuring the agent it names).
                    if (!agentIds.Contains(e.Agent))
                        errors.Add($"{where} measure_at names unknown agent '{e.Agent}'");
                    break;
                case EffectKind.SetGoal:
                case EffectKind.SetDecoherence:
                case EffectKind.Rearm:
                case EffectKind.Reward:
                    if (!agentIds.Contains(e.Agent))
                        errors.Add($"{where} {NameOf(e.Kind)} names unknown agent '{e.Agent}'");
                    else if (!ownedAgents.Contains(e.Agent))
                        errors.Add($"{where} {NameOf(e.Kind)} writes non-owned agent '{e.Agent}' (R2: writes are owner-only)");
                    break;
                case EffectKind.SetScale:
                case EffectKind.SetVisible:
                case EffectKind.PlaySound:
                    if (!bindingPorts.Contains(e.TargetBind))
                        errors.Add($"{where} {NameOf(e.Kind)} targets unknown binding '{e.TargetBind}'");
                    break;
                case EffectKind.Call:
                    if (string.IsNullOrEmpty(e.Fn))
                        errors.Add($"{where} call missing fn");
                    foreach (var a in e.Args)
                    {
                        if (a.Kind == CallArgKind.Bind && !bindingPorts.Contains(a.Bind))
                            errors.Add($"{where} call arg binds unknown binding '{a.Bind}'");
                        else if (a.Kind == CallArgKind.Agent && !agentIds.Contains(a.Agent))
                            errors.Add($"{where} call arg names unknown agent '{a.Agent}'");
                        else if (a.Kind == CallArgKind.Op && !pureIds.Contains(a.Op))
                            errors.Add($"{where} call arg references unknown op '{a.Op}'");
                    }
                    break;
            }
            if (e.Kind is EffectKind.SetGoal or EffectKind.SetDecoherence or EffectKind.MeasureAt
                or EffectKind.SetScale or EffectKind.SetVisible && e.Value == null)
                errors.Add($"{where} {NameOf(e.Kind)} missing value");
            CheckRef(e.Value, $"{where} {NameOf(e.Kind)}");
        }

        foreach (var s in c.States)
        {
            foreach (var e in s.Do) CheckEffect(e, $"state '{s.Id}' do");
            foreach (var e in s.Entry) CheckEffect(e, $"state '{s.Id}' entry");
            foreach (var e in s.Exit) CheckEffect(e, $"state '{s.Id}' exit");
            foreach (var t in s.Transitions)
            {
                if (!stateIds.Contains(t.Target))
                    errors.Add($"state '{s.Id}' transition targets unknown state '{t.Target}'");
                if (t.Trigger.Kind == TriggerKind.Commit && !agentIds.Contains(t.Trigger.Agent))
                    errors.Add($"state '{s.Id}' commit trigger names unknown agent '{t.Trigger.Agent}'");
                if (t.Trigger.Kind == TriggerKind.Guard)
                {
                    if (t.Trigger.Cond == null) errors.Add($"state '{s.Id}' guard missing cond");
                    else CheckRef(t.Trigger.Cond, $"state '{s.Id}' guard");
                }
                foreach (var e in t.Actions) CheckEffect(e, $"state '{s.Id}' transition action");
            }
        }

        foreach (var r in c.Regions)
        {
            if (!stateIds.Contains(r.Initial))
                errors.Add($"region '{r.Id}' initial state '{r.Initial}' is unknown");
            foreach (var sid in r.States)
                if (!stateIds.Contains(sid))
                    errors.Add($"region '{r.Id}' names unknown state '{sid}'");
        }
        if (c.Regions.Count == 0)
            errors.Add("chart has no regions");

        return errors;
    }

    private static bool HasPureCycle(Chart c)
    {
        int n = c.Pure.Count;
        var pos = new Dictionary<string, int>();
        for (int i = 0; i < n; i++) pos.TryAdd(c.Pure[i].Id, i);

        var indegree = new int[n];
        var consumers = new List<int>[n];
        for (int i = 0; i < n; i++) consumers[i] = new List<int>();
        for (int i = 0; i < n; i++)
            foreach (var dep in OpRefs(c.Pure[i]))
                if (pos.TryGetValue(dep, out var j))   // ignore dangling refs (reported elsewhere)
                {
                    consumers[j].Add(i);
                    indegree[i]++;
                }

        var q = new Queue<int>();
        for (int i = 0; i < n; i++)
            if (indegree[i] == 0) q.Enqueue(i);
        int seen = 0;
        while (q.Count > 0)
        {
            int i = q.Dequeue();
            seen++;
            foreach (var k in consumers[i])
                if (--indegree[k] == 0) q.Enqueue(k);
        }
        return seen != n;
    }

    // ---- JSON reading helpers -------------------------------------------

    private static JsonArray JArr<T>(IEnumerable<T> items, Func<T, JsonNode> project) =>
        new(items.Select(x => (JsonNode?)project(x)).ToArray());

    private static string Str(JsonNode n, string key, string fallback = "") =>
        n is JsonObject o && o.TryGetPropertyValue(key, out var v)
            && v is JsonValue jv && jv.TryGetValue<string>(out var s)
            ? s : fallback;

    private static double Num(JsonNode n, string key, double fallback = 0) =>
        n is JsonObject o && o.TryGetPropertyValue(key, out var v)
            && v is JsonValue jv && jv.TryGetValue<double>(out var d)
            ? d : fallback;

    private static bool Bool(JsonNode n, string key, bool fallback = false) =>
        n is JsonObject o && o.TryGetPropertyValue(key, out var v)
            && v is JsonValue jv && jv.TryGetValue<bool>(out var b)
            ? b : fallback;

    private static IEnumerable<JsonNode> Arr(JsonNode n, string key) =>
        n is JsonObject o && o.TryGetPropertyValue(key, out var v) && v is JsonArray a
            ? a.Where(x => x != null).Select(x => x!)
            : Enumerable.Empty<JsonNode>();

    private static IEnumerable<JsonNode?> ArrVals(JsonNode n, string key) =>
        n is JsonObject o && o.TryGetPropertyValue(key, out var v) && v is JsonArray a
            ? a
            : Enumerable.Empty<JsonNode?>();

    private static JsonNode Member(JsonNode n, string key) =>
        n is JsonObject o && o.TryGetPropertyValue(key, out var v) && v != null
            ? v
            : throw new StatechartFormatException($"missing '{key}'");

    private static string TargetBindOf(JsonNode e) =>
        e is JsonObject o && o.TryGetPropertyValue("target", out var t)
            && t is JsonObject to && to.TryGetPropertyValue("bind", out var b)
            && b is JsonValue jv && jv.TryGetValue<string>(out var s)
            ? s : "";
}
