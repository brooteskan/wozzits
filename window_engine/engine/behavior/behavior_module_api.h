#pragma once

// engine/behavior/behavior_module_api.h
//
// Small authoring layer for project behavior modules. This header stays on top
// of the C ABI, but gives module authors a simpler event-handler style.

#include <engine/behavior/behavior_plugin_abi.h>

#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <new>          // placement new (wz_instance_state)
#include <type_traits>  // is_trivially_copyable

#if defined(_WIN32)
#define WZ_BEHAVIOR_MODULE_EXPORT __declspec(dllexport)
#else
#define WZ_BEHAVIOR_MODULE_EXPORT __attribute__((visibility("default")))
#endif

#define WZ_BEHAVIOR_MODULE(module_name, handler_fn)                         \
    extern "C" WZ_BEHAVIOR_MODULE_EXPORT uint8_t wz_register_behaviors(     \
        WzBehaviorPluginApi* api)                                           \
    {                                                                       \
        if (!api || api->version != WZ_BEHAVIOR_ABI_VERSION                 \
            || !api->register_module)                                       \
        {                                                                   \
            return 0;                                                       \
        }                                                                   \
        return api->register_module(                                        \
            api->user,                                                      \
            module_name,                                                    \
            handler_fn,                                                     \
            nullptr);                                                       \
    }

#define WZ_BEHAVIOR_MODULE_EVENTS(                                          \
    module_name, handler_fn, event_channel_array)                           \
    extern "C" WZ_BEHAVIOR_MODULE_EXPORT uint8_t wz_register_behaviors(     \
        WzBehaviorPluginApi* api)                                           \
    {                                                                       \
        if (!api || api->version != WZ_BEHAVIOR_ABI_VERSION                 \
            || !api->register_module_desc)                                  \
        {                                                                   \
            return 0;                                                       \
        }                                                                   \
        const WzBehaviorModuleDesc desc = {                                 \
            sizeof(WzBehaviorModuleDesc),                                   \
            module_name,                                                    \
            handler_fn,                                                     \
            nullptr,                                                        \
            event_channel_array,                                            \
            (uint32_t)(sizeof(event_channel_array)                          \
                / sizeof((event_channel_array)[0])),                        \
            nullptr,                                                        \
        };                                                                  \
        return api->register_module_desc(api->user, &desc);                 \
    }

#define WZ_BEHAVIOR_MODULE_INIT(                                            \
    module_name, init_fn, handler_fn, event_channel_array)                   \
    extern "C" WZ_BEHAVIOR_MODULE_EXPORT uint8_t wz_register_behaviors(     \
        WzBehaviorPluginApi* api)                                           \
    {                                                                       \
        if (!api || api->version != WZ_BEHAVIOR_ABI_VERSION                 \
            || !api->register_module_desc)                                  \
        {                                                                   \
            return 0;                                                       \
        }                                                                   \
        const WzBehaviorModuleDesc desc = {                                 \
            sizeof(WzBehaviorModuleDesc),                                   \
            module_name,                                                    \
            handler_fn,                                                     \
            init_fn,                                                        \
            event_channel_array,                                            \
            (uint32_t)(sizeof(event_channel_array)                          \
                / sizeof((event_channel_array)[0])),                        \
            nullptr,                                                        \
        };                                                                  \
        return api->register_module_desc(api->user, &desc);                 \
    }

// Register a function-style behavior together with a declared param table (the
// "expose parameters globally" surface). The params describe the config
// tunables the behavior reads, with their types and authoring defaults, so the
// host registers them in one place and tools can enumerate them. Declaring
// params does not change how the behavior reads config at runtime
// (wz_config_float still works); it makes the params discoverable + documented.
static inline uint8_t wz_register_behavior_with_params(
    WzBehaviorPluginApi* api,
    const char* module,
    const char* name,
    WzBehaviorFn function,
    const WzBehaviorParamDesc* params,
    uint32_t param_count)
{
    if (!api || api->version != WZ_BEHAVIOR_ABI_VERSION
        || !api->register_behavior_desc)
    {
        return 0;
    }

    const WzBehaviorDesc desc = {
        (uint32_t)sizeof(WzBehaviorDesc),
        module,
        name,
        function,
        nullptr,  // behavior_user_data
        params,
        param_count,
    };
    return api->register_behavior_desc(api->user, &desc);
}

// The stack helper keeps jobs small and easy to author. The engine-side queue
// accepts more ports for generated/adapted jobs, but hand-written behavior
// modules should fit comfortably inside this helper limit.
#define WZ_GPU_MAX_JOB_PORTS 16u
#define WZ_GPU_MAX_KERNEL_CONTRACT_PORTS 16u

#define WZ_GPU_KERNEL(var, kernel_id_string)                                \
    WzGpuKernelPortContractDesc var##_ports[WZ_GPU_MAX_KERNEL_CONTRACT_PORTS] = {}; \
    uint32_t var##_port_count = 0u;                                          \
    const char* var##_kernel_id = kernel_id_string;

#define WZ_GPU_STRUCTURED_INPUT(var, name, element_type)                    \
    do {                                                                    \
        if (var##_port_count < WZ_GPU_MAX_KERNEL_CONTRACT_PORTS) {          \
            var##_ports[var##_port_count++] = WzGpuKernelPortContractDesc{  \
                name,                                                       \
                WZ_GPU_PORT_STRUCTURED_BUFFER,                              \
                WZ_GPU_PORT_INPUT,                                          \
                (uint32_t)sizeof(element_type),                             \
            };                                                              \
        }                                                                   \
    } while (0)

#define WZ_GPU_STRUCTURED_OUTPUT(var, name, element_type)                   \
    do {                                                                    \
        if (var##_port_count < WZ_GPU_MAX_KERNEL_CONTRACT_PORTS) {          \
            var##_ports[var##_port_count++] = WzGpuKernelPortContractDesc{  \
                name,                                                       \
                WZ_GPU_PORT_STRUCTURED_BUFFER,                              \
                WZ_GPU_PORT_OUTPUT,                                         \
                (uint32_t)sizeof(element_type),                             \
            };                                                              \
        }                                                                   \
    } while (0)

#define WZ_GPU_U32(var, name)                                               \
    do {                                                                    \
        if (var##_port_count < WZ_GPU_MAX_KERNEL_CONTRACT_PORTS) {          \
            var##_ports[var##_port_count++] = WzGpuKernelPortContractDesc{  \
                name, WZ_GPU_PORT_U32, WZ_GPU_PORT_INPUT, 0u,               \
            };                                                              \
        }                                                                   \
    } while (0)

#define WZ_GPU_F32(var, name)                                               \
    do {                                                                    \
        if (var##_port_count < WZ_GPU_MAX_KERNEL_CONTRACT_PORTS) {          \
            var##_ports[var##_port_count++] = WzGpuKernelPortContractDesc{  \
                name, WZ_GPU_PORT_F32, WZ_GPU_PORT_INPUT, 0u,               \
            };                                                              \
        }                                                                   \
    } while (0)

#define WZ_GPU_KERNEL_END(api, var)                                         \
    do {                                                                    \
        if ((api) && (api)->register_gpu_kernel_contract) {                 \
            const WzGpuKernelContractDesc var##_desc = {                    \
                var##_kernel_id,                                            \
                var##_ports,                                                \
                var##_port_count,                                           \
            };                                                              \
            (void)(api)->register_gpu_kernel_contract(                      \
                (api)->user,                                                \
                &var##_desc);                                               \
        }                                                                   \
    } while (0)

typedef struct WzGpuJob
{
    WzGpuComputeJobDesc desc;
    WzGpuPortValue ports[WZ_GPU_MAX_JOB_PORTS];
} WzGpuJob;

static inline void wz_gpu_job_clear(WzGpuJob* job)
{
    if (!job) {
        return;
    }
    memset(job, 0, sizeof(*job));
    job->desc.ports = job->ports;
    job->desc.group_count_x = 1u;
    job->desc.group_count_y = 1u;
    job->desc.group_count_z = 1u;
}

static inline uint8_t wz_gpu_begin(WzGpuJob* job, const char* kernel)
{
    if (!job || !kernel || !kernel[0]) {
        return 0u;
    }
    wz_gpu_job_clear(job);
    job->desc.kernel = kernel;
    return 1u;
}

static inline uint8_t wz_gpu_set_groups(
    WzGpuJob* job,
    uint32_t x,
    uint32_t y,
    uint32_t z)
{
    if (!job || x == 0u || y == 0u || z == 0u) {
        return 0u;
    }
    job->desc.group_count_x = x;
    job->desc.group_count_y = y;
    job->desc.group_count_z = z;
    return 1u;
}

/*
 * Declare the job's logical iteration domain and let the engine derive the
 * dispatch group count from that domain's element count and the kernel's
 * authored thread group size. This is the preferred way to size a dispatch:
 * the domain states what the kernel iterates, independently of which mesh
 * data it reads. Explicitly set group counts (wz_gpu_set_groups) always
 * take precedence.
 */
static inline uint8_t wz_gpu_set_dispatch_domain(
    WzGpuJob* job,
    WzGpuDispatchDomain domain)
{
    if (!job) {
        return 0u;
    }
    job->desc.dispatch_domain = domain;
    job->desc.group_count_x = 0u;
    job->desc.group_count_y = 1u;
    job->desc.group_count_z = 1u;
    return 1u;
}

/*
 * Legacy alias for wz_gpu_set_dispatch_domain(job,
 * WZ_GPU_DISPATCH_DOMAIN_AUTO): the engine derives the group count from the
 * largest element count it resolved for vertex-sized ports (vertex
 * positions input, mesh field output, vertex count constant). Topology
 * ports (indices, triangle count) do not feed AUTO; kernels iterating
 * topology declare an explicit FACE/CORNER domain instead.
 */
static inline uint8_t wz_gpu_set_groups_from_mesh(WzGpuJob* job)
{
    return wz_gpu_set_dispatch_domain(job, WZ_GPU_DISPATCH_DOMAIN_AUTO);
}

static inline uint8_t wz_gpu_set_request_tag(
    WzGpuJob* job,
    uint64_t request_tag)
{
    if (!job) {
        return 0u;
    }
    job->desc.request_tag = request_tag;
    return 1u;
}

static inline WzGpuPortValue* wz_gpu_add_port(
    WzGpuJob* job,
    const char* name,
    WzGpuPortKind kind,
    WzGpuPortDirection direction)
{
    if (!job || !name || !name[0]
        || kind == WZ_GPU_PORT_NONE
        || direction == 0u
        || job->desc.port_count >= WZ_GPU_MAX_JOB_PORTS)
    {
        return 0;
    }

    WzGpuPortValue* port = &job->ports[job->desc.port_count++];
    memset(port, 0, sizeof(*port));
    port->name = name;
    port->kind = kind;
    port->direction = direction;
    return port;
}

static inline uint8_t wz_gpu_set_structured_input(
    WzGpuJob* job,
    const char* name,
    uint32_t element_count,
    uint32_t stride_bytes,
    const void* initial_data,
    uint64_t initial_data_bytes)
{
    if (element_count == 0u || stride_bytes == 0u
        || (!initial_data && initial_data_bytes != 0u))
    {
        return 0u;
    }

    WzGpuPortValue* port = wz_gpu_add_port(
        job,
        name,
        WZ_GPU_PORT_STRUCTURED_BUFFER,
        WZ_GPU_PORT_INPUT);
    if (!port) {
        return 0u;
    }
    port->element_count = element_count;
    port->stride_bytes = stride_bytes;
    port->initial_data = initial_data;
    port->initial_data_bytes = initial_data_bytes;
    return 1u;
}

static inline uint8_t wz_gpu_set_structured_output(
    WzGpuJob* job,
    const char* name,
    uint32_t element_count,
    uint32_t stride_bytes)
{
    if (element_count == 0u || stride_bytes == 0u) {
        return 0u;
    }

    WzGpuPortValue* port = wz_gpu_add_port(
        job,
        name,
        WZ_GPU_PORT_STRUCTURED_BUFFER,
        WZ_GPU_PORT_OUTPUT);
    if (!port) {
        return 0u;
    }
    port->element_count = element_count;
    port->stride_bytes = stride_bytes;
    return 1u;
}

/*
 * Declare a structured float output that the engine publishes as the
 * entity's GPU-resident mesh field visualization buffer after dispatch.
 * channel_id 0 selects the entity's authored visualization channel.
 * element_count 0 lets the engine size the buffer from the target field's
 * element count (the mesh vertex count for vertex-domain fields).
 *
 * Published outputs are NOT read back into the completion event — the data
 * stays GPU-resident for rendering. If the publish fails (wrong element
 * count, no visualization target on the entity, ...), the readback happens
 * as usual and the failure reason is surfaced in the dispatch report.
 */
static inline uint8_t wz_gpu_set_structured_output_mesh_field(
    WzGpuJob* job,
    const char* name,
    uint32_t element_count,
    uint32_t channel_id)
{
    WzGpuPortValue* port = wz_gpu_add_port(
        job,
        name,
        WZ_GPU_PORT_STRUCTURED_BUFFER,
        WZ_GPU_PORT_OUTPUT);
    if (!port) {
        return 0u;
    }
    port->element_count = element_count;
    port->stride_bytes = sizeof(float);
    port->resource.value = WZ_GPU_RESOURCE_REF_MESH_FIELD_VISUALIZATION;
    port->u32[0] = channel_id;
    return 1u;
}

/*
 * Declare a structured input the engine fills with the entity's mesh vertex
 * positions (float3 per vertex, stride 12). The plugin uploads nothing; the
 * element count is the mesh vertex count, resolved through the entity's
 * mesh field visualization target.
 */
static inline uint8_t wz_gpu_set_structured_input_mesh_positions(
    WzGpuJob* job,
    const char* name)
{
    WzGpuPortValue* port = wz_gpu_add_port(
        job,
        name,
        WZ_GPU_PORT_STRUCTURED_BUFFER,
        WZ_GPU_PORT_INPUT);
    if (!port) {
        return 0u;
    }
    port->stride_bytes = 3u * sizeof(float);
    port->resource.value = WZ_GPU_RESOURCE_REF_MESH_VERTEX_POSITIONS;
    return 1u;
}

/*
 * Declare a u32 root-constant port the engine fills with the entity's mesh
 * vertex count (resolved through the entity's mesh field visualization
 * target). Use this instead of authoring the vertex count in behavior
 * config.
 */
static inline uint8_t wz_gpu_set_u32_mesh_vertex_count(
    WzGpuJob* job,
    const char* name)
{
    WzGpuPortValue* port = wz_gpu_add_port(
        job,
        name,
        WZ_GPU_PORT_U32,
        WZ_GPU_PORT_INPUT);
    if (!port) {
        return 0u;
    }
    port->resource.value = WZ_GPU_RESOURCE_REF_MESH_VERTEX_COUNT;
    return 1u;
}

/*
 * Declare a structured input the engine fills with the entity's mesh index
 * buffer (uint per index, stride 4, triangle list). The plugin uploads
 * nothing; the element count is the mesh index count, resolved through the
 * entity's mesh field visualization target.
 */
static inline uint8_t wz_gpu_set_structured_input_mesh_indices(
    WzGpuJob* job,
    const char* name)
{
    WzGpuPortValue* port = wz_gpu_add_port(
        job,
        name,
        WZ_GPU_PORT_STRUCTURED_BUFFER,
        WZ_GPU_PORT_INPUT);
    if (!port) {
        return 0u;
    }
    port->stride_bytes = sizeof(uint32_t);
    port->resource.value = WZ_GPU_RESOURCE_REF_MESH_INDICES;
    return 1u;
}

/*
 * Declare a u32 root-constant port the engine fills with the entity's mesh
 * triangle count (index count / 3, resolved through the entity's mesh field
 * visualization target). Use this instead of authoring the triangle count
 * in behavior config.
 */
static inline uint8_t wz_gpu_set_u32_mesh_triangle_count(
    WzGpuJob* job,
    const char* name)
{
    WzGpuPortValue* port = wz_gpu_add_port(
        job,
        name,
        WZ_GPU_PORT_U32,
        WZ_GPU_PORT_INPUT);
    if (!port) {
        return 0u;
    }
    port->resource.value = WZ_GPU_RESOURCE_REF_MESH_TRIANGLE_COUNT;
    return 1u;
}

/*
 * Declare a structured input the engine fills with one component of the
 * entity mesh's compiled sparse operator (component is a
 * WZ_GPU_SPARSE_OPERATOR_* value). The plugin uploads nothing; element
 * counts are engine-filled and the buffers are GPU-resident across
 * dispatches. Set port->u32[1] afterwards to select a non-default
 * operator kind.
 */
static inline uint8_t wz_gpu_set_structured_input_sparse_operator(
    WzGpuJob* job,
    const char* name,
    uint32_t component)
{
    WzGpuPortValue* port = wz_gpu_add_port(
        job,
        name,
        WZ_GPU_PORT_STRUCTURED_BUFFER,
        WZ_GPU_PORT_INPUT);
    if (!port) {
        return 0u;
    }
    port->stride_bytes = 4u;
    port->resource.value = WZ_GPU_RESOURCE_REF_MESH_SPARSE_OPERATOR;
    port->u32[0] = component;
    return 1u;
}

/*
 * Declare a u32 root-constant port the engine fills with sparse-operator
 * metadata ({row_count, nonzero_count, kind, value_convention}; the
 * kernel binding's root_constant_dwords selects how many it receives).
 * Lets the kernel validate it is consuming the operator it expects.
 */
static inline uint8_t wz_gpu_set_u32_sparse_operator_info(
    WzGpuJob* job,
    const char* name)
{
    WzGpuPortValue* port = wz_gpu_add_port(
        job,
        name,
        WZ_GPU_PORT_U32,
        WZ_GPU_PORT_INPUT);
    if (!port) {
        return 0u;
    }
    port->resource.value = WZ_GPU_RESOURCE_REF_MESH_SPARSE_OPERATOR_INFO;
    return 1u;
}

/*
 * Declare a structured input the engine fills with a Float1 channel from
 * the entity's MeshDerivedField (StructuredBuffer<float>, stride 4). v0 is
 * scalar-first and residual-mode only; vector fields should pass an
 * explicit component/magnitude mode when that support lands.
 */
static inline uint8_t wz_gpu_set_structured_input_mesh_field_signal(
    WzGpuJob* job,
    const char* name,
    uint32_t channel_id)
{
    WzGpuPortValue* port = wz_gpu_add_port(
        job,
        name,
        WZ_GPU_PORT_STRUCTURED_BUFFER,
        WZ_GPU_PORT_INPUT);
    if (!port) {
        return 0u;
    }
    port->stride_bytes = sizeof(float);
    port->resource.value = WZ_GPU_RESOURCE_REF_MESH_DERIVED_FIELD_CHANNEL;
    port->u32[0] = channel_id;
    port->u32[1] = WZ_GPU_MESH_FIELD_VALUE_FLOAT1;
    port->u32[2] = WZ_GPU_MESH_FIELD_COMPONENT_ALL;
    port->u32[3] = WZ_GPU_SPARSE_APPLY_RESIDUAL;
    return 1u;
}

static inline uint8_t wz_gpu_set_u32(
    WzGpuJob* job,
    const char* name,
    uint32_t value)
{
    WzGpuPortValue* port =
        wz_gpu_add_port(job, name, WZ_GPU_PORT_U32, WZ_GPU_PORT_INPUT);
    if (!port) {
        return 0u;
    }
    port->u32[0] = value;
    return 1u;
}

static inline uint8_t wz_gpu_set_f32(
    WzGpuJob* job,
    const char* name,
    float value)
{
    WzGpuPortValue* port =
        wz_gpu_add_port(job, name, WZ_GPU_PORT_F32, WZ_GPU_PORT_INPUT);
    if (!port) {
        return 0u;
    }
    port->f32[0] = value;
    return 1u;
}

static inline uint8_t wz_gpu_submit(
    const WzBehaviorFrameFacts* facts,
    const WzGpuJob* job,
    WzGpuWorkId* out_work)
{
    if (!facts || !facts->submit_gpu_compute || !job
        || !job->desc.kernel || !job->desc.kernel[0])
    {
        return 0u;
    }

    WzGpuComputeJobDesc desc = job->desc;
    desc.ports = job->ports;
    return facts->submit_gpu_compute(
        facts->gpu_compute_user,
        &desc,
        out_work);
}

static inline uint8_t wz_key_down(
    const WzBehaviorFrameFacts* facts,
    uint32_t key)
{
    return facts && facts->input && key < 256u
        ? facts->input->keyboard_down[key]
        : 0u;
}

static inline uint8_t wz_key_pressed(
    const WzBehaviorFrameFacts* facts,
    uint32_t key)
{
    return facts && facts->input && key < 256u
        ? facts->input->keyboard_pressed[key]
        : 0u;
}

static inline uint8_t wz_key_released(
    const WzBehaviorFrameFacts* facts,
    uint32_t key)
{
    return facts && facts->input && key < 256u
        ? facts->input->keyboard_released[key]
        : 0u;
}

static inline uint8_t wz_mouse_button_down(
    const WzBehaviorFrameFacts* facts,
    uint32_t button)
{
    return facts && facts->input && button < 3u
        ? facts->input->mouse_down[button]
        : 0u;
}

static inline uint8_t wz_mouse_button_pressed(
    const WzBehaviorFrameFacts* facts,
    uint32_t button)
{
    return facts && facts->input && button < 3u
        ? facts->input->mouse_pressed[button]
        : 0u;
}

static inline uint8_t wz_mouse_button_released(
    const WzBehaviorFrameFacts* facts,
    uint32_t button)
{
    return facts && facts->input && button < 3u
        ? facts->input->mouse_released[button]
        : 0u;
}

static inline int32_t wz_mouse_x(const WzBehaviorFrameFacts* facts)
{
    return facts && facts->input ? facts->input->mouse_x : 0;
}

static inline int32_t wz_mouse_y(const WzBehaviorFrameFacts* facts)
{
    return facts && facts->input ? facts->input->mouse_y : 0;
}

static inline int32_t wz_mouse_dx(const WzBehaviorFrameFacts* facts)
{
    return facts && facts->input ? facts->input->mouse_dx : 0;
}

static inline int32_t wz_mouse_dy(const WzBehaviorFrameFacts* facts)
{
    return facts && facts->input ? facts->input->mouse_dy : 0;
}

static inline uint8_t wz_window_focused(const WzBehaviorFrameFacts* facts)
{
    return facts && facts->input ? facts->input->window_focused : 0u;
}

static inline int32_t wz_window_width(const WzBehaviorFrameFacts* facts)
{
    return facts && facts->input ? facts->input->window_width : 0;
}

static inline int32_t wz_window_height(const WzBehaviorFrameFacts* facts)
{
    return facts && facts->input ? facts->input->window_height : 0;
}

static inline uint8_t wz_controller_count(
    const WzBehaviorFrameFacts* facts)
{
    return facts && facts->input ? facts->input->controller_count : 0u;
}

static inline uint8_t wz_controller_connected(
    const WzBehaviorFrameFacts* facts,
    uint32_t controller)
{
    return facts && facts->input && controller < facts->input->controller_count
        && controller < WZ_MAX_CONTROLLERS
        ? facts->input->controller_connected[controller]
        : 0u;
}

static inline uint8_t wz_controller_connected_pressed(
    const WzBehaviorFrameFacts* facts,
    uint32_t controller)
{
    return facts && facts->input && controller < facts->input->controller_count
        && controller < WZ_MAX_CONTROLLERS
        ? facts->input->controller_connected_pressed[controller]
        : 0u;
}

static inline uint8_t wz_controller_connected_released(
    const WzBehaviorFrameFacts* facts,
    uint32_t controller)
{
    return facts && facts->input && controller < facts->input->controller_count
        && controller < WZ_MAX_CONTROLLERS
        ? facts->input->controller_connected_released[controller]
        : 0u;
}

static inline float wz_controller_axis(
    const WzBehaviorFrameFacts* facts,
    uint32_t controller,
    uint32_t axis)
{
    return facts && facts->input && controller < facts->input->controller_count
        && controller < WZ_MAX_CONTROLLERS
        && axis < WZ_CONTROLLER_AXIS_COUNT
        ? facts->input->controller_axes[controller][axis]
        : 0.0f;
}

static inline uint8_t wz_controller_button_down(
    const WzBehaviorFrameFacts* facts,
    uint32_t controller,
    uint32_t button)
{
    return facts && facts->input && controller < facts->input->controller_count
        && controller < WZ_MAX_CONTROLLERS
        && button < WZ_CONTROLLER_BUTTON_COUNT
        ? facts->input->controller_buttons[controller][button]
        : 0u;
}

static inline uint8_t wz_controller_button_pressed(
    const WzBehaviorFrameFacts* facts,
    uint32_t controller,
    uint32_t button)
{
    return facts && facts->input && controller < facts->input->controller_count
        && controller < WZ_MAX_CONTROLLERS
        && button < WZ_CONTROLLER_BUTTON_COUNT
        ? facts->input->controller_buttons_pressed[controller][button]
        : 0u;
}

static inline uint8_t wz_controller_button_released(
    const WzBehaviorFrameFacts* facts,
    uint32_t controller,
    uint32_t button)
{
    return facts && facts->input && controller < facts->input->controller_count
        && controller < WZ_MAX_CONTROLLERS
        && button < WZ_CONTROLLER_BUTTON_COUNT
        ? facts->input->controller_buttons_released[controller][button]
        : 0u;
}

static inline uint8_t wz_input_wasd_axis(
    const WzBehaviorFrameFacts* facts,
    WzVec3* out_axis)
{
    if (!facts || !facts->input || !out_axis) {
        return 0;
    }

    out_axis->x =
        (wz_key_down(facts, WZ_KEY_D) ? 1.0f : 0.0f)
        - (wz_key_down(facts, WZ_KEY_A) ? 1.0f : 0.0f);
    out_axis->y = 0.0f;
    out_axis->z =
        (wz_key_down(facts, WZ_KEY_W) ? 1.0f : 0.0f)
        - (wz_key_down(facts, WZ_KEY_S) ? 1.0f : 0.0f);

    const float length =
        sqrtf(out_axis->x * out_axis->x
            + out_axis->z * out_axis->z);
    if (length > 1.0f) {
        const float inv_length = 1.0f / length;
        out_axis->x *= inv_length;
        out_axis->z *= inv_length;
    }
    return 1;
}

// Transform command semantics:
// - Commands are buffered during behavior dispatch and applied afterward.
// - Local translation writes the matrix translation column directly.
// - Local scale changes local basis-column lengths and preserves directions.
// - Local rotation uses WzQuaternion {x, y, z, w}; it replaces rotation,
//   preserves translation, and preserves current basis-column scale.
// - Linear velocity is in world units per second and is integrated once per
//   frame after behavior commands are applied.
// - AddLocalRotation is intentionally not part of this V1 API.

static inline uint8_t wz_write_add_local_translation(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    float x,
    float y,
    float z)
{
    if (!facts || !facts->write_command) {
        return 0;
    }

    const WzBehaviorCommand command = {
        entity,
        WZ_BEHAVIOR_COMMAND_ADD_LOCAL_TRANSLATION,
        { x, y, z, 0.0f },
    };
    return facts->write_command(facts->command_writer_user, &command);
}

static inline uint8_t wz_write_set_local_translation(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    float x,
    float y,
    float z)
{
    if (!facts || !facts->write_command) {
        return 0;
    }

    const WzBehaviorCommand command = {
        entity,
        WZ_BEHAVIOR_COMMAND_SET_LOCAL_TRANSLATION,
        { x, y, z, 0.0f },
    };
    return facts->write_command(facts->command_writer_user, &command);
}

static inline uint8_t wz_write_add_world_translation(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    float x,
    float y,
    float z)
{
    if (!facts || !facts->write_command) {
        return 0;
    }

    const WzBehaviorCommand command = {
        entity,
        WZ_BEHAVIOR_COMMAND_ADD_WORLD_TRANSLATION,
        { x, y, z, 0.0f },
    };
    return facts->write_command(facts->command_writer_user, &command);
}

static inline uint8_t wz_write_set_world_translation(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    float x,
    float y,
    float z)
{
    if (!facts || !facts->write_command) {
        return 0;
    }

    const WzBehaviorCommand command = {
        entity,
        WZ_BEHAVIOR_COMMAND_SET_WORLD_TRANSLATION,
        { x, y, z, 0.0f },
    };
    return facts->write_command(facts->command_writer_user, &command);
}

static inline uint8_t wz_write_add_local_scale(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    float x,
    float y,
    float z)
{
    if (!facts || !facts->write_command) {
        return 0;
    }

    const WzBehaviorCommand command = {
        entity,
        WZ_BEHAVIOR_COMMAND_ADD_LOCAL_SCALE,
        { x, y, z, 0.0f },
    };
    return facts->write_command(facts->command_writer_user, &command);
}

static inline uint8_t wz_write_set_local_scale(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    float x,
    float y,
    float z)
{
    if (!facts || !facts->write_command) {
        return 0;
    }

    const WzBehaviorCommand command = {
        entity,
        WZ_BEHAVIOR_COMMAND_SET_LOCAL_SCALE,
        { x, y, z, 0.0f },
    };
    return facts->write_command(facts->command_writer_user, &command);
}

static inline uint8_t wz_write_set_local_rotation(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    WzQuaternion rotation)
{
    if (!facts || !facts->write_command) {
        return 0;
    }

    const WzBehaviorCommand command = {
        entity,
        WZ_BEHAVIOR_COMMAND_SET_LOCAL_ROTATION,
        { rotation.x, rotation.y, rotation.z, rotation.w },
    };
    return facts->write_command(facts->command_writer_user, &command);
}

static inline uint8_t wz_write_set_linear_velocity(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    float x,
    float y,
    float z)
{
    if (!facts || !facts->write_command) {
        return 0;
    }

    const WzBehaviorCommand command = {
        entity,
        WZ_BEHAVIOR_COMMAND_SET_LINEAR_VELOCITY,
        { x, y, z, 0.0f },
    };
    return facts->write_command(facts->command_writer_user, &command);
}

static inline uint8_t wz_write_set_angular_velocity(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    float x,
    float y,
    float z)
{
    if (!facts || !facts->write_command) {
        return 0;
    }

    const WzBehaviorCommand command = {
        entity,
        WZ_BEHAVIOR_COMMAND_SET_ANGULAR_VELOCITY,
        { x, y, z, 0.0f },
    };
    return facts->write_command(facts->command_writer_user, &command);
}

static inline uint8_t wz_write_set_motion_space(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    WzBehaviorMotionSpace space)
{
    if (!facts || !facts->write_command) {
        return 0;
    }

    const WzBehaviorCommand command = {
        entity,
        WZ_BEHAVIOR_COMMAND_SET_MOTION_SPACE,
        { (float)space, 0.0f, 0.0f, 0.0f },
    };
    return facts->write_command(facts->command_writer_user, &command);
}

// Set how fast `entity`'s terrain-constrained Motion swings its up-axis toward
// the surface normal, in radians/second. <= 0 restores the instantaneous
// strength-based alignment. Applied by apply_behavior_commands the same frame
// the terrain pass runs; the value is runtime-only, so an aligned actor re-
// asserts it each frame (cheap, idempotent, survives a scene rebuild/spawn).
static inline uint8_t wz_write_set_terrain_alignment_rate(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    float rate_radians_per_second)
{
    if (!facts || !facts->write_command) {
        return 0;
    }

    const WzBehaviorCommand command = {
        entity,
        WZ_BEHAVIOR_COMMAND_SET_TERRAIN_ALIGNMENT_RATE,
        { rate_radians_per_second, 0.0f, 0.0f, 0.0f },
    };
    return facts->write_command(facts->command_writer_user, &command);
}

// Audio (audio-track item 9). `entity` is the node carrying the AudioSource
// component — find it with wz_find_entity_by_authored_id / wz_find_entity_by_name
// (or pass self). The host resolves it to that AudioSource and posts to the audio
// scheduler; a no-op if the entity has no AudioSource. One AudioSource per node,
// so the entity is the whole address. Audio plays in PLAY mode only.

// Play the entity's AudioSource (uses the renderable's baked gain/pitch/looping).
// Plays the renderable's default clip: values[0] = -1 is the host's "use
// default_index" sentinel, so this matches auto-play. A single-clip renderable
// has only one clip, so it plays that. Use wz_write_play_sound_indexed to pick a
// specific bank clip.
static inline uint8_t wz_write_play_sound(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity)
{
    if (!facts || !facts->write_command) {
        return 0;
    }

    const WzBehaviorCommand command = {
        entity,
        WZ_BEHAVIOR_COMMAND_PLAY_SOUND,
        { -1.0f, 0.0f, 0.0f, 0.0f },
    };
    return facts->write_command(facts->command_writer_user, &command);
}

// Play a specific clip of the entity's AudioSource by index. For a bank-backed
// renderable this selects entry `clip_index`; an out-of-range index falls back to
// the renderable's default clip. The index is carried in values[0] (where the
// host reads v0). A single-clip renderable ignores the index (it has one clip).
static inline uint8_t wz_write_play_sound_indexed(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    uint32_t clip_index)
{
    if (!facts || !facts->write_command) {
        return 0;
    }

    const WzBehaviorCommand command = {
        entity,
        WZ_BEHAVIOR_COMMAND_PLAY_SOUND,
        { (float)clip_index, 0.0f, 0.0f, 0.0f },
    };
    return facts->write_command(facts->command_writer_user, &command);
}

// FNV-1a/32 over a clip name. constexpr, so a plugin can hash a string literal at
// COMPILE time (zero runtime cost) and pass the result to wz_write_play_sound_hashed:
//   static constexpr uint32_t kFire = wz_play_sound_hash("Canon_b");
//   wz_write_play_sound_hashed(facts, e, kFire);
// Must match the engine's clip-name hash (the bank stores the same FNV-1a/32).
static inline constexpr uint32_t wz_play_sound_hash(const char* name)
{
    uint32_t h = 2166136261u;
    if (name) {
        for (const char* p = name; *p; ++p) {
            h ^= (uint32_t)(unsigned char)(*p);
            h *= 16777619u;
        }
    }
    return h;
}

// Play a clip of the entity's AudioSource selected by name hash. The host looks
// the hash up in the renderable's clip names; an unknown name falls back to the
// default clip. Encoding: values[0] = -2 (name-select sentinel), values[1] carries
// the 32-bit hash as a bit pattern (a container, not a numeric value), so the full
// 32 bits survive the float slot. Prefer the compile-time wz_play_sound_hash().
static inline uint8_t wz_write_play_sound_hashed(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    uint32_t name_hash)
{
    if (!facts || !facts->write_command) {
        return 0;
    }

    float hash_bits = 0.0f;
    memcpy(&hash_bits, &name_hash, sizeof(uint32_t));

    const WzBehaviorCommand command = {
        entity,
        WZ_BEHAVIOR_COMMAND_PLAY_SOUND,
        { -2.0f, hash_bits, 0.0f, 0.0f },
    };
    return facts->write_command(facts->command_writer_user, &command);
}

// Convenience: play a clip by name, hashing at the call site. For a hot trigger,
// prefer wz_write_play_sound_hashed(facts, e, wz_play_sound_hash("name")) so the
// hash is computed at compile time.
static inline uint8_t wz_write_play_sound_named(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    const char* name)
{
    return wz_write_play_sound_hashed(facts, entity, wz_play_sound_hash(name));
}

// Stop the entity's AudioSource. fade_frames > 0 ramps it out (de-click); 0 cuts.
static inline uint8_t wz_write_stop_sound(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    float fade_frames)
{
    if (!facts || !facts->write_command) {
        return 0;
    }

    const WzBehaviorCommand command = {
        entity,
        WZ_BEHAVIOR_COMMAND_STOP_SOUND,
        { fade_frames, 0.0f, 0.0f, 0.0f },
    };
    return facts->write_command(facts->command_writer_user, &command);
}

// Ramp the entity's AudioSource gain to `gain` over `ramp_frames` (0 = jump).
static inline uint8_t wz_write_set_sound_gain(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    float gain,
    float ramp_frames)
{
    if (!facts || !facts->write_command) {
        return 0;
    }

    const WzBehaviorCommand command = {
        entity,
        WZ_BEHAVIOR_COMMAND_SET_SOUND_GAIN,
        { gain, ramp_frames, 0.0f, 0.0f },
    };
    return facts->write_command(facts->command_writer_user, &command);
}

// Ramp a live grain-cloud parameter on the entity's AudioSource (only takes
// effect if its renderable is a grain cloud). `param` is a WZ_GRAIN_PARAM_*
// ordinal; `value` is the target; `ramp_frames` de-zippers the change (0 = jump).
// The smoothing runs on the audio thread, so this is safe to call every frame.
static inline uint8_t wz_write_set_grain_param(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    uint32_t param,
    float value,
    float ramp_frames)
{
    if (!facts || !facts->write_command) {
        return 0;
    }

    const WzBehaviorCommand command = {
        entity,
        WZ_BEHAVIOR_COMMAND_SET_GRAIN_PARAM,
        { (float)param, value, ramp_frames, 0.0f },
    };
    return facts->write_command(facts->command_writer_user, &command);
}

// Typed conveniences for the lean live grain set.
static inline uint8_t wz_write_set_grain_gain(
    const WzBehaviorFrameFacts* facts, WzBehaviorEntityId entity,
    float gain, float ramp_frames)
{
    return wz_write_set_grain_param(
        facts, entity, WZ_GRAIN_PARAM_GAIN, gain, ramp_frames);
}

static inline uint8_t wz_write_set_grain_density(
    const WzBehaviorFrameFacts* facts, WzBehaviorEntityId entity,
    float grains_per_second, float ramp_frames)
{
    return wz_write_set_grain_param(
        facts, entity, WZ_GRAIN_PARAM_DENSITY, grains_per_second, ramp_frames);
}

static inline uint8_t wz_write_set_grain_position(
    const WzBehaviorFrameFacts* facts, WzBehaviorEntityId entity,
    float normalized, float ramp_frames)
{
    return wz_write_set_grain_param(
        facts, entity, WZ_GRAIN_PARAM_POSITION, normalized, ramp_frames);
}

static inline uint8_t wz_write_set_grain_pitch(
    const WzBehaviorFrameFacts* facts, WzBehaviorEntityId entity,
    float multiplier, float ramp_frames)
{
    return wz_write_set_grain_param(
        facts, entity, WZ_GRAIN_PARAM_PITCH, multiplier, ramp_frames);
}

// Source-blend LFO: rate (cycles/sec, 0 = off) and depth (0..1). Ramp these to
// shift the texture's blend when, say, the player enters a new region.
static inline uint8_t wz_write_set_grain_blend_rate(
    const WzBehaviorFrameFacts* facts, WzBehaviorEntityId entity,
    float rate_hz, float ramp_frames)
{
    return wz_write_set_grain_param(
        facts, entity, WZ_GRAIN_PARAM_BLEND_RATE, rate_hz, ramp_frames);
}

static inline uint8_t wz_write_set_grain_blend_depth(
    const WzBehaviorFrameFacts* facts, WzBehaviorEntityId entity,
    float depth, float ramp_frames)
{
    return wz_write_set_grain_param(
        facts, entity, WZ_GRAIN_PARAM_BLEND_DEPTH, depth, ramp_frames);
}

// Ramp one clip's spawn weight in the addressed cloud's palette. `source_index`
// is the clip's index in the node's bank (0-based). Weight 0 = exclude the clip;
// when every weight reaches ~0 the cloud goes silent. This is the primitive a
// program is built from; ramp over frames to crossfade which clips voice the
// texture. Encodes the index in values[3] (see WZ_GRAIN_PARAM_SOURCE_WEIGHT).
static inline uint8_t wz_write_set_grain_source_weight(
    const WzBehaviorFrameFacts* facts, WzBehaviorEntityId entity,
    uint32_t source_index, float weight, float ramp_frames)
{
    if (!facts || !facts->write_command) {
        return 0;
    }
    const WzBehaviorCommand command = {
        entity,
        WZ_BEHAVIOR_COMMAND_SET_GRAIN_PARAM,
        { (float)WZ_GRAIN_PARAM_SOURCE_WEIGHT, weight, ramp_frames,
          (float)source_index },
    };
    return facts->write_command(facts->command_writer_user, &command);
}

// A complete grain-synth preset: which clips (per-source weights over the
// addressed node's fixed bank) PLUS every grain parameter. A "program". Fill one
// in your behavior (a plain struct), then wz_write_grain_program crossfades the
// whole preset toward it — the "switch program 1 -> program 2 over N frames" op.
// Weights index the node's bank palette (set at scene load, not changeable here);
// weight 0 excludes a clip. `window` is a WZ_GRAIN_WINDOW_* shape (discrete: it
// snaps, no crossfade). All other fields crossfade over the ramp.
typedef struct WzGrainProgram
{
    float    weights[WZ_GRAIN_MAX_SOURCES];  // per-clip spawn weight; 0 = exclude
    uint32_t weight_count;                   // clips addressed (<= bank size)
    float    gain;                           // mix level
    float    density;                        // grains/sec
    float    position;                       // 0..1 playhead (0.5 = clip middle)
    float    pitch;                          // multiplier
    float    grain_ms;                       // grain duration (large = slow grains)
    float    position_jitter;                // 0..1 spread around position
    float    pitch_jitter_semitones;         // +/- semitone spread
    float    pan_center;                     // -1..1
    float    pan_spread;                     // 0..1
    float    window_param;                   // window shape parameter
    uint32_t window;                         // WZ_GRAIN_WINDOW_* (snaps)
    float    blend_rate;                     // autonomous blend LFO Hz (0 = off)
    float    blend_depth;                    // 0..1
} WzGrainProgram;

// Crossfade the addressed cloud toward the complete preset `program` over
// `ramp_frames` output frames (~sample_rate frames = 1 second). Every param ramps
// so the whole texture morphs coherently; window shape snaps. Fires one command
// per param (cheap, once per switch); each smooths on the audio thread. No-op
// (returns 0) without a valid program. Harmless if the entity isn't a grain cloud
// (the mixer only matches grain slots).
static inline uint8_t wz_write_grain_program(
    const WzBehaviorFrameFacts* facts, WzBehaviorEntityId entity,
    const WzGrainProgram* program, float ramp_frames)
{
    if (!facts || !facts->write_command || !program) {
        return 0;
    }
    uint32_t n = program->weight_count;
    if (n > WZ_GRAIN_MAX_SOURCES) {
        n = WZ_GRAIN_MAX_SOURCES;
    }
    for (uint32_t i = 0; i < n; ++i) {
        wz_write_set_grain_source_weight(
            facts, entity, i, program->weights[i], ramp_frames);
    }
    wz_write_set_grain_gain(facts, entity, program->gain, ramp_frames);
    wz_write_set_grain_density(facts, entity, program->density, ramp_frames);
    wz_write_set_grain_position(facts, entity, program->position, ramp_frames);
    wz_write_set_grain_pitch(facts, entity, program->pitch, ramp_frames);
    wz_write_set_grain_param(facts, entity, WZ_GRAIN_PARAM_GRAIN_MS,
                             program->grain_ms, ramp_frames);
    wz_write_set_grain_param(facts, entity, WZ_GRAIN_PARAM_POSITION_JITTER,
                             program->position_jitter, ramp_frames);
    wz_write_set_grain_param(facts, entity, WZ_GRAIN_PARAM_PITCH_JITTER,
                             program->pitch_jitter_semitones, ramp_frames);
    wz_write_set_grain_param(facts, entity, WZ_GRAIN_PARAM_PAN_CENTER,
                             program->pan_center, ramp_frames);
    wz_write_set_grain_param(facts, entity, WZ_GRAIN_PARAM_PAN_SPREAD,
                             program->pan_spread, ramp_frames);
    wz_write_set_grain_param(facts, entity, WZ_GRAIN_PARAM_WINDOW_PARAM,
                             program->window_param, ramp_frames);
    wz_write_set_grain_blend_rate(facts, entity, program->blend_rate, ramp_frames);
    wz_write_set_grain_blend_depth(facts, entity, program->blend_depth, ramp_frames);
    // Window SHAPE is discrete → snap (ramp 0).
    wz_write_set_grain_param(facts, entity, WZ_GRAIN_PARAM_WINDOW,
                             (float)program->window, 0.0f);
    return 1;
}

// FNV-1a/32 over a prefab name. constexpr, so a plugin can hash a string literal
// at COMPILE time (zero runtime cost) and pass the result to wz_write_spawn_-
// prefab:
//   static constexpr uint32_t kEnemy = wz_prefab_hash("enemy_tank");
//   wz_write_spawn_prefab_hashed(facts, e, kEnemy, 0.f, 0.f, -5.f);
// Must match the engine's prefab-name hash (the host hashes the Scene asset name
// the same way). Identical FNV-1a/32 to wz_play_sound_hash.
static inline constexpr uint32_t wz_prefab_hash(const char* name)
{
    uint32_t h = 2166136261u;
    if (name) {
        for (const char* p = name; *p; ++p) {
            h ^= (uint32_t)(unsigned char)(*p);
            h *= 16777619u;
        }
    }
    return h;
}

// Spawn a prefab ("scenelet" Scene asset) at a transform derived from `entity`
// (the spawner): the host computes T = spawner world transform × the offset and
// grafts a conflict-free clone of the prefab's nodes. Host-handled (it does not
// mutate `entity`); applied in the per-frame command pass. Encoding: values[0]
// carries the 32-bit prefab name hash as a bit pattern (a container, not a
// numeric value, so the full 32 bits survive the float slot), values[1..3] = the
// offset xyz in the spawner's frame. Play-mode only. Prefer the compile-time
// wz_prefab_hash().
static inline uint8_t wz_write_spawn_prefab_hashed(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    uint32_t prefab_name_hash,
    float offset_x,
    float offset_y,
    float offset_z)
{
    if (!facts || !facts->write_command) {
        return 0;
    }

    float hash_bits = 0.0f;
    memcpy(&hash_bits, &prefab_name_hash, sizeof(uint32_t));

    const WzBehaviorCommand command = {
        entity,
        WZ_BEHAVIOR_COMMAND_SPAWN_PREFAB,
        { hash_bits, offset_x, offset_y, offset_z },
    };
    return facts->write_command(facts->command_writer_user, &command);
}

// Convenience: spawn a prefab by name, hashing at the call site. For a hot
// trigger, prefer wz_write_spawn_prefab_hashed(.., wz_prefab_hash("name"), ..)
// so the hash is computed at compile time.
static inline uint8_t wz_write_spawn_prefab(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    const char* prefab_name,
    float offset_x,
    float offset_y,
    float offset_z)
{
    return wz_write_spawn_prefab_hashed(
        facts, entity, wz_prefab_hash(prefab_name),
        offset_x, offset_y, offset_z);
}

// FNV-1a/32 over a renderable-constant name. constexpr, so a plugin can hash a
// string literal at COMPILE time:
//   static constexpr uint32_t kTint = wz_renderable_param_hash("tint");
//   wz_write_set_renderable_param(facts, e, kTint, r, g, b);
// Must match the host's field-name hash (the host hashes the layout's declared
// constant names the same way). Identical FNV-1a/32 to wz_prefab_hash.
static inline constexpr uint32_t wz_renderable_param_hash(const char* name)
{
    uint32_t h = 2166136261u;
    if (name) {
        for (const char* p = name; *p; ++p) {
            h ^= (uint32_t)(unsigned char)(*p);
            h *= 16777619u;
        }
    }
    return h;
}

// Set an authored renderable CONSTANT on `entity`'s look (issue #232): the host
// resolves `param_name_hash` to one of the node's declared constant fields and
// writes the node's per-instance override, which the next frame's pack merges
// into the draw packet — no re-key, no recompile (the #229 seam). Host-handled
// (it does not mutate `entity`); applied in the per-frame command pass. A no-op
// if the hash names no declared or already-overridden constant on the node.
//
// Encoding: values[0] carries the 32-bit name hash as a bit pattern (a
// container, not a numeric value, so the full 32 bits survive the float slot),
// values[1..3] = the new x/y/z (r/g/b). With only four value slots the name
// consumes one, so a float4 field's fourth component (w / alpha) is PRESERVED,
// not carried. Prefer the compile-time wz_renderable_param_hash().
static inline uint8_t wz_write_set_renderable_param(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    uint32_t param_name_hash,
    float x,
    float y,
    float z)
{
    if (!facts || !facts->write_command) {
        return 0;
    }

    float hash_bits = 0.0f;
    memcpy(&hash_bits, &param_name_hash, sizeof(uint32_t));

    const WzBehaviorCommand command = {
        entity,
        WZ_BEHAVIOR_COMMAND_SET_RENDERABLE_PARAM,
        { hash_bits, x, y, z },
    };
    return facts->write_command(facts->command_writer_user, &command);
}

// Convenience: set a renderable constant by name, hashing at the call site. For
// a hot per-frame pulse, prefer wz_write_set_renderable_param(.., wz_renderable_
// param_hash("name"), ..) so the hash is computed at compile time.
static inline uint8_t wz_write_set_renderable_param_named(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    const char* param_name,
    float x,
    float y,
    float z)
{
    return wz_write_set_renderable_param(
        facts, entity, wz_renderable_param_hash(param_name), x, y, z);
}

// Point the runtime scene camera at `camera_entity` -- a node carrying a camera
// component (found by name/id, or a spawned prefab's camera). Host-handled like
// the audio commands: apply_behavior_commands ignores it; WozzitsApp_v1 reads the
// node's transform + camera params and switches the active view (per frame, not
// only at scene load). A no-op if the entity has no camera component. Play-mode
// only -- the editor records the anchor but stays on the free-fly edit camera.
// This is the built-in scene_camera behavior's action, callable from ANY module
// (e.g. spawn_player activating the freshly spawned player's camera).
static inline uint8_t wz_write_set_active_camera(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId camera_entity)
{
    if (!facts || !facts->write_command) {
        return 0;
    }
    const WzBehaviorCommand command = {
        camera_entity,
        WZ_BEHAVIOR_COMMAND_SET_ACTIVE_CAMERA,
        { 0.0f, 0.0f, 0.0f, 0.0f },
    };
    return facts->write_command(facts->command_writer_user, &command);
}

// Deferred runtime-authoring: ask the runtime to spawn a new child node under
// `parent_entity`. The add is applied at the next frame boundary through the
// shared runtime apply path (it does not mutate the scene during dispatch), and
// it is fire-and-forget — no id comes back. Returns 1 if the request was queued
// (the parent resolved to an authored scene node), 0 otherwise.
static inline uint8_t wz_spawn_child(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId parent_entity)
{
    if (!facts || !facts->spawn_child) {
        return 0;
    }
    return facts->spawn_child(facts->deferred_authoring_user, parent_entity);
}

// Deferred runtime-authoring: ask the runtime to remove the node bound to
// `entity` (and its subtree). Applied at the next frame boundary through the
// shared runtime apply path (it does not mutate the scene during dispatch),
// fire-and-forget. Returns 1 if the request was queued (the entity resolved to
// an authored scene node), 0 otherwise.
static inline uint8_t wz_remove_node(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity)
{
    if (!facts || !facts->remove_node) {
        return 0;
    }
    return facts->remove_node(facts->deferred_authoring_user, entity);
}

// Deferred runtime-authoring: set the preferred asset-graph renderable of the
// node bound to `entity` (0 clears it). Applied at the next frame boundary
// through the shared runtime apply path (it does not mutate the scene during
// dispatch), fire-and-forget. Returns 1 if the request was queued (the entity
// resolved to an authored scene node), 0 otherwise.
static inline uint8_t wz_set_renderable_asset(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    uint64_t asset_graph_node_id)
{
    if (!facts || !facts->set_renderable_asset) {
        return 0;
    }
    return facts->set_renderable_asset(
        facts->deferred_authoring_user, entity, asset_graph_node_id);
}

// Deferred runtime-authoring: reparent the node bound to `entity` under the
// node bound to `new_parent_entity` (pass WZ_INVALID_BEHAVIOR_ENTITY to detach
// to the top level). Applied at the next frame boundary through the shared
// runtime apply path (it does not mutate the scene during dispatch),
// fire-and-forget. Returns 1 if the request was queued (the entity resolved to
// an authored scene node), 0 otherwise.
static inline uint8_t wz_reparent_node(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    WzBehaviorEntityId new_parent_entity)
{
    if (!facts || !facts->reparent_node) {
        return 0;
    }
    return facts->reparent_node(
        facts->deferred_authoring_user, entity, new_parent_entity);
}

// Deferred runtime-authoring: add the optional component `kind`
// ("camera" | "proximity" | "collision" | "motion") to the node bound to
// `entity`. Applied at the next frame boundary through the shared runtime apply
// path (it does not mutate the scene during dispatch), fire-and-forget. The
// `kind` string need only stay valid for this call. Returns 1 if the request
// was queued (the entity resolved to an authored scene node), 0 otherwise.
static inline uint8_t wz_add_node_component(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    const char* kind)
{
    if (!facts || !facts->add_node_component) {
        return 0;
    }
    return facts->add_node_component(
        facts->deferred_authoring_user, entity, kind);
}

// Deferred runtime-authoring: remove the optional component `kind`
// ("camera" | "proximity" | "collision" | "motion") from the node bound to
// `entity`. Applied at the next frame boundary through the shared runtime apply
// path (it does not mutate the scene during dispatch), fire-and-forget. The
// `kind` string need only stay valid for this call. Returns 1 if the request
// was queued (the entity resolved to an authored scene node), 0 otherwise.
static inline uint8_t wz_remove_node_component(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    const char* kind)
{
    if (!facts || !facts->remove_node_component) {
        return 0;
    }
    return facts->remove_node_component(
        facts->deferred_authoring_user, entity, kind);
}

static inline float wz_delta_seconds(const WzBehaviorFrameFacts* facts)
{
    return facts && facts->timing ? facts->timing->delta_seconds : 0.0f;
}

static inline uint64_t wz_frame_index(const WzBehaviorFrameFacts* facts)
{
    return facts && facts->timing ? facts->timing->frame_index : 0u;
}

// Absolute accumulated simulation time in seconds (a monotonic clock). Unlike
// wz_delta_seconds this is total sim-time, the input an agent's cognition clock
// stamps against. 0 when the host wires no cognition scheduler.
static inline double wz_sim_time(const WzBehaviorFrameFacts* facts)
{
    return facts ? facts->sim_time : 0.0;
}

// Self-paced wake: from a cognition.tick handler, request the next wake
// `delay_seconds` of sim-time from now (<= 0 = next cognition pass). A handler
// that does not call this sleeps until something else reschedules it. Returns 1
// if accepted.
static inline uint8_t wz_set_next_wake(
    const WzBehaviorFrameFacts* facts,
    double delay_seconds)
{
    if (!facts || !facts->set_next_wake) {
        return 0;
    }
    return facts->set_next_wake(facts->wake_scheduler_user, delay_seconds);
}

// Cognition read surface (decider/actuator split). Read the quantum_agent decision
// on `entity` -- the committed disposition (out->committed: -1 deliberating, 0/1
// the chosen outcome) + live marginal (out->marginal: <sigma_z> in [-1, 1]).
// Returns 1 and fills `out` if that node hosts a quantum_agent, else 0. An actuator
// behavior calls this to act on a decider's choice without owning the cognition.
static inline uint8_t wz_agent_decision(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    WzAgentDecision* out)
{
    if (!facts || !facts->get_agent_decision || !out) {
        return 0;
    }
    return facts->get_agent_decision(
        facts->cognition_reader_user, entity, out);
}

// Convenience: read the quantum_agent co-located on the event's own entity (self).
static inline uint8_t wz_self_agent_decision(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    WzAgentDecision* out)
{
    return wz_agent_decision(
        facts,
        event ? event->entity : (WzBehaviorEntityId)WZ_INVALID_BEHAVIOR_ENTITY,
        out);
}

// Read one of a quantum_agent's several coupled decisions by index (0 matches
// wz_agent_decision). Returns 0 if the node has no quantum_agent or the index is
// out of range / the host wires no reader.
static inline uint8_t wz_agent_decision_at(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    uint32_t agent_index,
    WzAgentDecision* out)
{
    if (!facts || !facts->get_agent_decision_at || !out) {
        return 0;
    }
    return facts->get_agent_decision_at(
        facts->cognition_reader_user, entity, agent_index, out);
}

// Convenience: read decision `agent_index` of the agent co-located on self.
static inline uint8_t wz_self_agent_decision_at(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    uint32_t agent_index,
    WzAgentDecision* out)
{
    return wz_agent_decision_at(
        facts,
        event ? event->entity : (WzBehaviorEntityId)WZ_INVALID_BEHAVIOR_ENTITY,
        agent_index,
        out);
}

// Re-bias one of a quantum_agent's decisions (write surface). Takes effect on the
// agent's next think; pair with wz_rearm_agent to re-open an already-committed
// decision. Returns 0 if the node has no quantum_agent / the index is out of range.
static inline uint8_t wz_set_agent_goal(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    uint32_t agent_index,
    float field)
{
    if (!facts || !facts->set_agent_goal) {
        return 0;
    }
    return facts->set_agent_goal(
        facts->cognition_reader_user, entity, agent_index, field);
}

// Re-open (re-deliberate) all of a quantum_agent's decisions: clear the latches +
// restart the anneal. Returns 0 if the node has no quantum_agent.
static inline uint8_t wz_rearm_agent(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity)
{
    if (!facts || !facts->rearm_agent) {
        return 0;
    }
    return facts->rearm_agent(facts->cognition_reader_user, entity);
}

// Convenience: re-bias / re-arm the agent co-located on self.
static inline uint8_t wz_self_set_agent_goal(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    uint32_t agent_index,
    float field)
{
    return wz_set_agent_goal(
        facts,
        event ? event->entity : (WzBehaviorEntityId)WZ_INVALID_BEHAVIOR_ENTITY,
        agent_index,
        field);
}

static inline uint8_t wz_self_rearm_agent(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event)
{
    return wz_rearm_agent(
        facts,
        event ? event->entity : (WzBehaviorEntityId)WZ_INVALID_BEHAVIOR_ENTITY);
}

// Resize a hub/group agent to `member_count` star-bonded members (dynamic squad
// membership). Returns 0 if the node has no quantum_agent / the rebuild fails.
static inline uint8_t wz_reshape_group(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    uint32_t member_count,
    float star_coupling)
{
    if (!facts || !facts->reshape_group) {
        return 0;
    }
    return facts->reshape_group(
        facts->cognition_reader_user, entity, member_count, star_coupling);
}

static inline uint8_t wz_self_reshape_group(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    uint32_t member_count,
    float star_coupling)
{
    return wz_reshape_group(
        facts,
        event ? event->entity : (WzBehaviorEntityId)WZ_INVALID_BEHAVIOR_ENTITY,
        member_count,
        star_coupling);
}

// LEARNING: reinforce a quantum_agent's memory register toward the branch
// (memory_qubit == `toward`) by `strength` (> 0 reward, < 0 punish). Monotonic +
// saturating; survives rearm/reshape/commit. Returns 0 if the node has no
// quantum_agent / no memory / bad qubit.
static inline uint8_t wz_agent_reward(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    uint32_t memory_qubit,
    uint8_t toward,
    float strength)
{
    if (!facts || !facts->reward_agent) {
        return 0;
    }
    return facts->reward_agent(
        facts->cognition_reader_user, entity, memory_qubit, toward, strength);
}

// Read what a quantum_agent's memory learned about `memory_qubit`: <sigma_z> in
// [-1, 1] (+1 leans toward the `toward == true` branch). 0 if no memory.
static inline float wz_agent_memory(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    uint32_t memory_qubit)
{
    if (!facts || !facts->agent_memory) {
        return 0.0f;
    }
    return facts->agent_memory(
        facts->cognition_reader_user, entity, memory_qubit);
}

// Convenience: reinforce / read the memory of the agent co-located on self.
static inline uint8_t wz_self_agent_reward(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    uint32_t memory_qubit,
    uint8_t toward,
    float strength)
{
    return wz_agent_reward(
        facts,
        event ? event->entity : (WzBehaviorEntityId)WZ_INVALID_BEHAVIOR_ENTITY,
        memory_qubit,
        toward,
        strength);
}

static inline float wz_self_agent_memory(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    uint32_t memory_qubit)
{
    return wz_agent_memory(
        facts,
        event ? event->entity : (WzBehaviorEntityId)WZ_INVALID_BEHAVIOR_ENTITY,
        memory_qubit);
}

// CONTEXTUAL LEARNING: reinforce the joint (context, action) branch of a
// quantum_agent's memory (needs memory >= 2). Rewarding the diagonal learns a
// different action per context. Returns 0 if the node has no quantum_agent / no
// memory / bad qubit.
static inline uint8_t wz_agent_reward_pair(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    uint32_t ctx_qubit,
    uint8_t ctx_value,
    uint32_t dec_qubit,
    uint8_t dec_value,
    float strength)
{
    if (!facts || !facts->reward_agent_pair) {
        return 0;
    }
    return facts->reward_agent_pair(
        facts->cognition_reader_user, entity,
        ctx_qubit, ctx_value, dec_qubit, dec_value, strength);
}

// Read the learned action for a context WITHOUT measuring: <sigma_z> of dec_qubit
// given ctx_qubit == ctx_value, in [-1, 1] (+1 => |0>). 0 if no memory.
static inline float wz_agent_conditional_pref(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    uint32_t ctx_qubit,
    uint8_t ctx_value,
    uint32_t dec_qubit)
{
    if (!facts || !facts->agent_conditional_pref) {
        return 0.0f;
    }
    return facts->agent_conditional_pref(
        facts->cognition_reader_user, entity, ctx_qubit, ctx_value, dec_qubit);
}

// Convenience: contextual reward / conditional read on the agent co-located on self.
static inline uint8_t wz_self_agent_reward_pair(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    uint32_t ctx_qubit,
    uint8_t ctx_value,
    uint32_t dec_qubit,
    uint8_t dec_value,
    float strength)
{
    return wz_agent_reward_pair(
        facts,
        event ? event->entity : (WzBehaviorEntityId)WZ_INVALID_BEHAVIOR_ENTITY,
        ctx_qubit, ctx_value, dec_qubit, dec_value, strength);
}

static inline float wz_self_agent_conditional_pref(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    uint32_t ctx_qubit,
    uint8_t ctx_value,
    uint32_t dec_qubit)
{
    return wz_agent_conditional_pref(
        facts,
        event ? event->entity : (WzBehaviorEntityId)WZ_INVALID_BEHAVIOR_ENTITY,
        ctx_qubit, ctx_value, dec_qubit);
}

// Set the co-located quantum_agent's decoherence rate live (observation-forced
// decoherence: high -> snap/predictable commit, ~0 -> stays coherent). Returns 0
// if the node has no quantum_agent.
static inline uint8_t wz_set_agent_decoherence(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    float rate)
{
    if (!facts || !facts->set_agent_decoherence) {
        return 0;
    }
    return facts->set_agent_decoherence(
        facts->cognition_reader_user, entity, rate);
}

static inline uint8_t wz_self_set_agent_decoherence(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    float rate)
{
    return wz_set_agent_decoherence(
        facts,
        event ? event->entity : (WzBehaviorEntityId)WZ_INVALID_BEHAVIOR_ENTITY,
        rate);
}

static inline void wz_log_info(
    const WzBehaviorFrameFacts* facts,
    const char* message)
{
    if (facts && facts->log_info && message) {
        facts->log_info(facts->log_user, message);
    }
}

static inline void wz_log_infof(
    const WzBehaviorFrameFacts* facts,
    const char* format,
    ...)
{
    if (!facts || !facts->log_info || !format) {
        return;
    }

    char message[512];
    va_list args;
    va_start(args, format);
    const int written = vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    if (written < 0) {
        return;
    }
    message[sizeof(message) - 1u] = '\0';
    wz_log_info(facts, message);
}

static inline void wz_log_info(
    const WzBehaviorInitFacts* facts,
    const char* message)
{
    if (facts && facts->log_info && message) {
        facts->log_info(facts->log_user, message);
    }
}

static inline void wz_log_infof(
    const WzBehaviorInitFacts* facts,
    const char* format,
    ...)
{
    if (!facts || !facts->log_info || !format) {
        return;
    }

    char message[512];
    va_list args;
    va_start(args, format);
    const int written = vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    if (written < 0) {
        return;
    }
    message[sizeof(message) - 1u] = '\0';
    wz_log_info(facts, message);
}

static inline void wz_log_info(decltype(nullptr), const char*)
{
}

static inline void wz_log_infof(decltype(nullptr), const char*, ...)
{
}

static inline void* wz_alloc_instance_state(
    const WzBehaviorInitFacts* facts,
    uint32_t size,
    uint32_t alignment)
{
    return facts && facts->alloc_instance_state
        ? facts->alloc_instance_state(
            facts->behavior_state_user,
            size,
            alignment)
        : nullptr;
}

static inline void* wz_alloc_instance_state_desc(
    const WzBehaviorInitFacts* facts,
    const WzBehaviorStateDesc* desc)
{
    return facts && facts->alloc_instance_state_desc
        ? facts->alloc_instance_state_desc(
            facts->behavior_state_user,
            desc)
        : nullptr;
}

static inline void* wz_get_instance_state(
    const WzBehaviorInitFacts* facts)
{
    return facts && facts->get_instance_state
        ? facts->get_instance_state(facts->behavior_state_user)
        : nullptr;
}

static inline void* wz_get_instance_state(
    const WzBehaviorFrameFacts* facts)
{
    return facts && facts->get_instance_state
        ? facts->get_instance_state(facts->behavior_state_user)
        : nullptr;
}

// Typed instance state — the idiomatic way to hold a behavior's per-node state.
//
//   void tank_init(const WzBehaviorInitFacts* facts, WzBehaviorEntityId, void*) {
//       TankState* s = wz_instance_state<TankState>(facts);   // constructed
//       if (s) wz_find_entity_by_authored_id(facts, "1:camera", &s->canon_audio);
//   }
//   void on_event(const WzBehaviorFrameFacts* facts, ...) {
//       TankState* s = wz_instance_state<TankState>(facts);   // fetched
//       if (!s) return;
//   }
//
// From on_init this allocates the block ON FIRST init and constructs T in place,
// so T's default member initializers actually run (unlike a raw void* cast). On a
// later init (hot reload) the preserved block is returned AS-IS — state survives
// the reload and is NOT re-initialized. From a frame fact it just fetches the
// existing block. Returns nullptr if no state is available.
//
// T must be trivially copyable: instance state is plain data the host preserves
// as raw bytes across reloads — it is never destructed, so no RAII members.
template <typename T>
static inline T* wz_instance_state(const WzBehaviorInitFacts* facts)
{
    static_assert(std::is_trivially_copyable<T>::value,
        "behavior instance state must be trivially copyable (plain data)");

    if (void* existing = wz_get_instance_state(facts)) {
        return static_cast<T*>(existing);  // reuse preserved block (reload)
    }
    void* raw = wz_alloc_instance_state(
        facts, (uint32_t)sizeof(T), (uint32_t)alignof(T));
    return raw ? new (raw) T{} : nullptr;  // first init: construct (defaults run)
}

template <typename T>
static inline T* wz_instance_state(const WzBehaviorFrameFacts* facts)
{
    return static_cast<T*>(wz_get_instance_state(facts));
}

// ── Peer instance state: read ANOTHER entity's behavior data by handle ──────────
//
// The read half of cross-entity behavior communication. Given a handle (e.g. a
// collision / proximity event's `other`, from wz_other(event)) and the module
// name of a behavior bound to it, returns a pointer to that behavior's instance
// state -- or null if the entity has no such behavior binding. The reading + the
// owning behavior agree on the struct layout via a SHARED HEADER (like the tanks
// share cannon_fire.h); the engine defines no schema. Event-agnostic -- it takes a
// handle, not an event, so it works from any of the collision/proximity events (or
// any handle at all).
//
//   // projectile.h -- shared by the projectile AND every collision handler
//   struct ProjectileState { WzBehaviorEntityId shooter; double fired_at; float damage; };
//
//   // a tank's COLLISION_ENTER / PROXIMITY_ENTER handler:
//   if (auto* p = wz_instance_state_of<ProjectileState>(
//           facts, wz_other(event), "projectile")) {
//       take_damage(p->damage); credit(p->shooter);
//   }
//
// Read it immediately; do NOT cache the pointer across frames (a rebuild can
// relocate the state block).
static inline void* wz_get_instance_state_of(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    const char* module_name)
{
    return facts && facts->get_instance_state_of
        ? facts->get_instance_state_of(
            facts->behavior_state_user, entity, module_name)
        : nullptr;
}

template <typename T>
static inline T* wz_instance_state_of(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    const char* module_name)
{
    static_assert(std::is_trivially_copyable<T>::value,
        "peer behavior state must be trivially copyable (plain data)");
    return static_cast<T*>(
        wz_get_instance_state_of(facts, entity, module_name));
}

static inline void* wz_create_shared_state(
    const WzBehaviorInitFacts* facts,
    const char* key,
    uint32_t size,
    uint32_t alignment)
{
    return facts && facts->create_shared_state
        ? facts->create_shared_state(
            facts->behavior_state_user,
            key,
            size,
            alignment)
        : nullptr;
}

static inline void* wz_create_shared_state_desc(
    const WzBehaviorInitFacts* facts,
    const char* key,
    const WzBehaviorStateDesc* desc)
{
    return facts && facts->create_shared_state_desc
        ? facts->create_shared_state_desc(
            facts->behavior_state_user,
            key,
            desc)
        : nullptr;
}

static inline void* wz_find_shared_state(
    const WzBehaviorInitFacts* facts,
    const char* key)
{
    return facts && facts->find_shared_state
        ? facts->find_shared_state(facts->behavior_state_user, key)
        : nullptr;
}

static inline void* wz_find_shared_state(
    const WzBehaviorFrameFacts* facts,
    const char* key)
{
    return facts && facts->find_shared_state
        ? facts->find_shared_state(facts->behavior_state_user, key)
        : nullptr;
}

static inline void* wz_find_shared_state(decltype(nullptr), const char*)
{
    return nullptr;
}

static inline WzBehaviorEntityId wz_self(const WzBehaviorEvent* event)
{
    return event ? event->entity : WZ_INVALID_BEHAVIOR_ENTITY;
}

static inline WzBehaviorEventKind wz_event_kind(const WzBehaviorEvent* event)
{
    return event ? event->kind : WZ_EVENT_NONE;
}

static inline uint8_t wz_is_event(
    const WzBehaviorEvent* event,
    WzBehaviorEventKind kind)
{
    return wz_event_kind(event) == kind ? 1u : 0u;
}

static inline WzBehaviorEntityId wz_other(const WzBehaviorEvent* event)
{
    return event ? event->other : WZ_INVALID_BEHAVIOR_ENTITY;
}

static inline uint32_t wz_input_event_key(
    const WzBehaviorFrameFacts* facts)
{
    return facts && facts->active_input_event
        ? facts->active_input_event->key
        : WZ_INPUT_EVENT_INVALID_VALUE;
}

static inline uint32_t wz_input_event_mouse_button(
    const WzBehaviorFrameFacts* facts)
{
    return facts && facts->active_input_event
        ? facts->active_input_event->button
        : WZ_INPUT_EVENT_INVALID_VALUE;
}

static inline uint32_t wz_input_event_controller(
    const WzBehaviorFrameFacts* facts)
{
    return facts && facts->active_input_event
        ? facts->active_input_event->controller
        : WZ_INPUT_EVENT_INVALID_VALUE;
}

static inline uint32_t wz_input_event_controller_button(
    const WzBehaviorFrameFacts* facts)
{
    return facts && facts->active_input_event
        ? facts->active_input_event->button
        : WZ_INPUT_EVENT_INVALID_VALUE;
}

static inline uint32_t wz_input_event_controller_axis(
    const WzBehaviorFrameFacts* facts)
{
    return facts && facts->active_input_event
        ? facts->active_input_event->axis
        : WZ_INPUT_EVENT_INVALID_VALUE;
}

static inline float wz_input_event_controller_axis_value(
    const WzBehaviorFrameFacts* facts)
{
    return facts && facts->active_input_event
        ? facts->active_input_event->value
        : 0.0f;
}

static inline uint8_t wz_gpu_compute_event_active(
    const WzBehaviorFrameFacts* facts)
{
    return facts && facts->active_gpu_compute_event ? 1u : 0u;
}

static inline WzGpuWorkId wz_gpu_compute_event_work(
    const WzBehaviorFrameFacts* facts)
{
    return facts && facts->active_gpu_compute_event
        ? facts->active_gpu_compute_event->work
        : WzGpuWorkId{ 0u };
}

static inline WzGpuComputeStatus wz_gpu_compute_event_status(
    const WzBehaviorFrameFacts* facts)
{
    return facts && facts->active_gpu_compute_event
        ? facts->active_gpu_compute_event->status
        : WZ_GPU_COMPUTE_STATUS_NONE;
}

static inline uint64_t wz_gpu_compute_event_request_tag(
    const WzBehaviorFrameFacts* facts)
{
    return facts && facts->active_gpu_compute_event
        ? facts->active_gpu_compute_event->request_tag
        : 0u;
}

static inline uint32_t wz_gpu_compute_event_output_count(
    const WzBehaviorFrameFacts* facts)
{
    return facts && facts->active_gpu_compute_event
        ? facts->active_gpu_compute_event->output_count
        : 0u;
}

static inline const WzGpuComputeOutputView* wz_gpu_compute_event_output(
    const WzBehaviorFrameFacts* facts,
    uint32_t index)
{
    if (!facts
        || !facts->active_gpu_compute_event
        || !facts->active_gpu_compute_event->outputs
        || index >= facts->active_gpu_compute_event->output_count)
    {
        return 0;
    }
    return &facts->active_gpu_compute_event->outputs[index];
}

static inline const WzGpuComputeOutputView* wz_gpu_compute_event_find_output(
    const WzBehaviorFrameFacts* facts,
    const char* name)
{
    if (!facts
        || !facts->active_gpu_compute_event
        || !facts->active_gpu_compute_event->outputs
        || !name
        || !name[0])
    {
        return 0;
    }

    const WzGpuComputeEventPayload* event =
        facts->active_gpu_compute_event;
    for (uint32_t i = 0; i < event->output_count; ++i) {
        const WzGpuComputeOutputView* output = &event->outputs[i];
        if (output->name && strcmp(output->name, name) == 0) {
            return output;
        }
    }
    return 0;
}

static inline uint32_t wz_gpu_compute_output_element_count(
    const WzBehaviorFrameFacts* facts,
    const char* name)
{
    const WzGpuComputeOutputView* output =
        wz_gpu_compute_event_find_output(facts, name);
    return output ? output->element_count : 0u;
}

static inline uint32_t wz_gpu_compute_output_stride_bytes(
    const WzBehaviorFrameFacts* facts,
    const char* name)
{
    const WzGpuComputeOutputView* output =
        wz_gpu_compute_event_find_output(facts, name);
    return output ? output->stride_bytes : 0u;
}

static inline uint64_t wz_gpu_compute_output_byte_count(
    const WzBehaviorFrameFacts* facts,
    const char* name)
{
    const WzGpuComputeOutputView* output =
        wz_gpu_compute_event_find_output(facts, name);
    return output ? output->byte_count : 0u;
}

static inline uint8_t wz_gpu_compute_read_output(
    const WzBehaviorFrameFacts* facts,
    const char* name,
    void* out_bytes,
    uint64_t out_byte_capacity,
    uint64_t* out_required_bytes)
{
    const WzGpuComputeOutputView* output =
        wz_gpu_compute_event_find_output(facts, name);
    if (!output || !output->bytes) {
        if (out_required_bytes) {
            *out_required_bytes = 0u;
        }
        return 0u;
    }
    if (out_required_bytes) {
        *out_required_bytes = output->byte_count;
    }
    if (!out_bytes || out_byte_capacity < output->byte_count) {
        return 0u;
    }
    memcpy(out_bytes, output->bytes, (size_t)output->byte_count);
    return 1u;
}

static inline uint8_t wz_gpu_compute_read_output_u32(
    const WzBehaviorFrameFacts* facts,
    const char* name,
    uint32_t index,
    uint32_t* out_value)
{
    const WzGpuComputeOutputView* output =
        wz_gpu_compute_event_find_output(facts, name);
    if (!output
        || !out_value
        || output->kind != WZ_GPU_PORT_STRUCTURED_BUFFER
        || output->stride_bytes != sizeof(uint32_t)
        || index >= output->element_count
        || !output->bytes
        || output->byte_count
            < ((uint64_t)index + 1u) * sizeof(uint32_t))
    {
        return 0u;
    }

    const uint8_t* bytes = (const uint8_t*)output->bytes;
    memcpy(out_value, bytes + (uint64_t)index * sizeof(uint32_t),
        sizeof(uint32_t));
    return 1u;
}

static inline uint8_t wz_self_is_trigger(const WzBehaviorEvent* event)
{
    return event ? event->self_is_trigger : 0u;
}

static inline uint8_t wz_self_add_local_translation(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    float x,
    float y,
    float z)
{
    return wz_write_add_local_translation(facts, wz_self(event), x, y, z);
}

static inline uint8_t wz_self_set_local_translation(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    float x,
    float y,
    float z)
{
    return wz_write_set_local_translation(facts, wz_self(event), x, y, z);
}

static inline uint8_t wz_self_add_world_translation(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    float x,
    float y,
    float z)
{
    return wz_write_add_world_translation(facts, wz_self(event), x, y, z);
}

static inline uint8_t wz_self_set_world_translation(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    float x,
    float y,
    float z)
{
    return wz_write_set_world_translation(facts, wz_self(event), x, y, z);
}

static inline uint8_t wz_other_add_world_translation(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    float x,
    float y,
    float z)
{
    return wz_write_add_world_translation(facts, wz_other(event), x, y, z);
}

static inline uint8_t wz_other_set_world_translation(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    float x,
    float y,
    float z)
{
    return wz_write_set_world_translation(facts, wz_other(event), x, y, z);
}

static inline uint8_t wz_self_add_local_scale(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    float x,
    float y,
    float z)
{
    return wz_write_add_local_scale(facts, wz_self(event), x, y, z);
}

static inline uint8_t wz_self_set_local_scale(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    float x,
    float y,
    float z)
{
    return wz_write_set_local_scale(facts, wz_self(event), x, y, z);
}

static inline uint8_t wz_self_set_local_rotation(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    WzQuaternion rotation)
{
    return wz_write_set_local_rotation(facts, wz_self(event), rotation);
}

static inline uint8_t wz_self_set_linear_velocity(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    float x,
    float y,
    float z)
{
    return wz_write_set_linear_velocity(facts, wz_self(event), x, y, z);
}

static inline uint8_t wz_self_set_angular_velocity(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    float x,
    float y,
    float z)
{
    return wz_write_set_angular_velocity(facts, wz_self(event), x, y, z);
}

static inline uint8_t wz_self_set_motion_space(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    WzBehaviorMotionSpace space)
{
    return wz_write_set_motion_space(facts, wz_self(event), space);
}

// Set the event's own entity's terrain-alignment slew rate (radians/sec). <= 0
// restores the instantaneous strength-based alignment. The value is runtime-only
// Motion state, so re-assert it (e.g. once per frame, or each time you adjust
// motion) to keep it set across a scene rebuild.
static inline uint8_t wz_self_set_terrain_alignment_rate(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    float rate_radians_per_second)
{
    return wz_write_set_terrain_alignment_rate(
        facts, wz_self(event), rate_radians_per_second);
}

// Deferred runtime-authoring convenience: spawn a child under the event's own
// entity. Fire-and-forget, applied at the next frame boundary (see
// wz_spawn_child).
static inline uint8_t wz_self_spawn_child(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event)
{
    return wz_spawn_child(facts, wz_self(event));
}

// Deferred runtime-authoring convenience: remove the event's own entity.
// Fire-and-forget, applied at the next frame boundary (see wz_remove_node).
static inline uint8_t wz_self_remove_node(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event)
{
    return wz_remove_node(facts, wz_self(event));
}

// Deferred runtime-authoring convenience: set the preferred asset-graph
// renderable of the event's own entity (0 clears it). Fire-and-forget, applied
// at the next frame boundary (see wz_set_renderable_asset).
static inline uint8_t wz_self_set_renderable_asset(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    uint64_t asset_graph_node_id)
{
    return wz_set_renderable_asset(facts, wz_self(event), asset_graph_node_id);
}

// Deferred runtime-authoring convenience: reparent the event's own entity under
// `new_parent_entity` (pass WZ_INVALID_BEHAVIOR_ENTITY to detach to the top
// level). Fire-and-forget, applied at the next frame boundary (see
// wz_reparent_node).
static inline uint8_t wz_self_reparent_node(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    WzBehaviorEntityId new_parent_entity)
{
    return wz_reparent_node(facts, wz_self(event), new_parent_entity);
}

// Deferred runtime-authoring convenience: detach the event's own entity to the
// top level (no parent). Fire-and-forget, applied at the next frame boundary
// (see wz_reparent_node).
static inline uint8_t wz_self_detach_to_top_level(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event)
{
    return wz_reparent_node(facts, wz_self(event), WZ_INVALID_BEHAVIOR_ENTITY);
}

// Deferred runtime-authoring convenience: add the optional component `kind` to
// the event's own entity. Fire-and-forget, applied at the next frame boundary
// (see wz_add_node_component).
static inline uint8_t wz_self_add_node_component(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    const char* kind)
{
    return wz_add_node_component(facts, wz_self(event), kind);
}

// Deferred runtime-authoring convenience: remove the optional component `kind`
// from the event's own entity. Fire-and-forget, applied at the next frame
// boundary (see wz_remove_node_component).
static inline uint8_t wz_self_remove_node_component(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    const char* kind)
{
    return wz_remove_node_component(facts, wz_self(event), kind);
}

static inline uint8_t wz_other_set_linear_velocity(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    float x,
    float y,
    float z)
{
    return wz_write_set_linear_velocity(facts, wz_other(event), x, y, z);
}

static inline uint8_t wz_other_set_angular_velocity(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    float x,
    float y,
    float z)
{
    return wz_write_set_angular_velocity(facts, wz_other(event), x, y, z);
}

static inline uint8_t wz_other_set_motion_space(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    WzBehaviorMotionSpace space)
{
    return wz_write_set_motion_space(facts, wz_other(event), space);
}

static inline uint8_t wz_read_local_transform(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    WzMat4* out_transform)
{
    if (!facts || !facts->get_local_transform) {
        return 0;
    }
    return facts->get_local_transform(
        facts->transform_query_user,
        entity,
        out_transform);
}

static inline uint8_t wz_read_world_transform(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    WzMat4* out_transform)
{
    if (!facts || !facts->get_world_transform) {
        return 0;
    }
    return facts->get_world_transform(
        facts->transform_query_user,
        entity,
        out_transform);
}

static inline uint8_t wz_read_local_position(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    WzVec3* out_position)
{
    if (!facts || !facts->get_local_position) {
        return 0;
    }
    return facts->get_local_position(
        facts->transform_query_user,
        entity,
        out_position);
}

static inline uint8_t wz_read_world_position(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    WzVec3* out_position)
{
    if (!facts || !facts->get_world_position) {
        return 0;
    }
    return facts->get_world_position(
        facts->transform_query_user,
        entity,
        out_position);
}

static inline uint8_t wz_read_local_transform(
    const WzBehaviorInitFacts* facts,
    WzBehaviorEntityId entity,
    WzMat4* out_transform)
{
    if (!facts || !facts->get_local_transform) {
        return 0;
    }
    return facts->get_local_transform(
        facts->transform_query_user,
        entity,
        out_transform);
}

static inline uint8_t wz_read_world_transform(
    const WzBehaviorInitFacts* facts,
    WzBehaviorEntityId entity,
    WzMat4* out_transform)
{
    if (!facts || !facts->get_world_transform) {
        return 0;
    }
    return facts->get_world_transform(
        facts->transform_query_user,
        entity,
        out_transform);
}

static inline uint8_t wz_read_local_position(
    const WzBehaviorInitFacts* facts,
    WzBehaviorEntityId entity,
    WzVec3* out_position)
{
    if (!facts || !facts->get_local_position) {
        return 0;
    }
    return facts->get_local_position(
        facts->transform_query_user,
        entity,
        out_position);
}

static inline uint8_t wz_read_world_position(
    const WzBehaviorInitFacts* facts,
    WzBehaviorEntityId entity,
    WzVec3* out_position)
{
    if (!facts || !facts->get_world_position) {
        return 0;
    }
    return facts->get_world_position(
        facts->transform_query_user,
        entity,
        out_position);
}

static inline uint8_t wz_self_local_transform(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    WzMat4* out_transform)
{
    return wz_read_local_transform(facts, wz_self(event), out_transform);
}

static inline uint8_t wz_self_world_transform(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    WzMat4* out_transform)
{
    return wz_read_world_transform(facts, wz_self(event), out_transform);
}

static inline uint8_t wz_self_local_position(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    WzVec3* out_position)
{
    return wz_read_local_position(facts, wz_self(event), out_position);
}

static inline uint8_t wz_self_world_position(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    WzVec3* out_position)
{
    return wz_read_world_position(facts, wz_self(event), out_position);
}

static inline uint8_t wz_other_local_transform(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    WzMat4* out_transform)
{
    return wz_read_local_transform(facts, wz_other(event), out_transform);
}

static inline uint8_t wz_other_world_transform(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    WzMat4* out_transform)
{
    return wz_read_world_transform(facts, wz_other(event), out_transform);
}

static inline uint8_t wz_other_local_position(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    WzVec3* out_position)
{
    return wz_read_local_position(facts, wz_other(event), out_position);
}

static inline uint8_t wz_other_world_position(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    WzVec3* out_position)
{
    return wz_read_world_position(facts, wz_other(event), out_position);
}

static inline uint8_t wz_vector_between_world_positions(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId from_entity,
    WzBehaviorEntityId to_entity,
    WzVec3* out_vector)
{
    if (!out_vector) {
        return 0;
    }

    WzVec3 from{};
    WzVec3 to{};
    if (!wz_read_world_position(facts, from_entity, &from)
        || !wz_read_world_position(facts, to_entity, &to))
    {
        return 0;
    }

    out_vector->x = to.x - from.x;
    out_vector->y = to.y - from.y;
    out_vector->z = to.z - from.z;
    return 1;
}

static inline uint8_t wz_vector_self_to_other(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    WzVec3* out_vector)
{
    return wz_vector_between_world_positions(
        facts,
        wz_self(event),
        wz_other(event),
        out_vector);
}

static inline uint8_t wz_distance_between_world_positions(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId from_entity,
    WzBehaviorEntityId to_entity,
    float* out_distance)
{
    if (!out_distance) {
        return 0;
    }

    WzVec3 vector{};
    if (!wz_vector_between_world_positions(
            facts,
            from_entity,
            to_entity,
            &vector))
    {
        return 0;
    }

    *out_distance =
        sqrtf(vector.x * vector.x
            + vector.y * vector.y
            + vector.z * vector.z);
    return 1;
}

static inline uint8_t wz_distance_self_to_other(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    float* out_distance)
{
    return wz_distance_between_world_positions(
        facts,
        wz_self(event),
        wz_other(event),
        out_distance);
}

static inline uint8_t wz_direction_between_world_positions(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId from_entity,
    WzBehaviorEntityId to_entity,
    WzVec3* out_direction)
{
    if (!out_direction) {
        return 0;
    }

    WzVec3 vector{};
    if (!wz_vector_between_world_positions(
            facts,
            from_entity,
            to_entity,
            &vector))
    {
        return 0;
    }

    const float length =
        sqrtf(vector.x * vector.x
            + vector.y * vector.y
            + vector.z * vector.z);
    if (length <= 0.000001f) {
        out_direction->x = 0.0f;
        out_direction->y = 0.0f;
        out_direction->z = 0.0f;
        return 0;
    }

    const float inv_length = 1.0f / length;
    out_direction->x = vector.x * inv_length;
    out_direction->y = vector.y * inv_length;
    out_direction->z = vector.z * inv_length;
    return 1;
}

static inline uint8_t wz_direction_self_to_other(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    WzVec3* out_direction)
{
    return wz_direction_between_world_positions(
        facts,
        wz_self(event),
        wz_other(event),
        out_direction);
}

static inline uint8_t wz_query_collision_surface_ray(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId surface_entity,
    WzVec3 origin,
    WzVec3 direction,
    float max_distance,
    WzSurfaceSample* out_sample)
{
    if (!facts || !facts->query_collision_surface_ray) {
        return 0;
    }
    return facts->query_collision_surface_ray(
        facts->collision_query_user,
        surface_entity,
        origin,
        direction,
        max_distance,
        out_sample);
}

static inline uint8_t wz_sample_terrain_surface(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId terrain_entity,
    float world_x,
    float world_z,
    WzSurfaceSample* out_sample)
{
    if (!facts || !facts->sample_terrain_surface) {
        return 0;
    }
    return facts->sample_terrain_surface(
        facts->collision_query_user,
        terrain_entity,
        world_x,
        world_z,
        out_sample);
}

static inline uint8_t wz_find_entity_by_name(
    const WzBehaviorFrameFacts* facts,
    const char* name,
    WzBehaviorEntityId* out_entity)
{
    if (!facts || !facts->find_entity_by_name) {
        return 0;
    }
    return facts->find_entity_by_name(
        facts->scene_query_user,
        name,
        out_entity);
}

// Resolve a node by its authored id. `authored_id` may carry an optional ":name"
// suffix for readability, e.g. "1:camera": the id (before ':') is the stable key
// that's matched; if a name follows, the node's current name must also match
// (a typo or post-rename mismatch returns 0 instead of the wrong node). Returns 1
// and writes out_entity on a match, 0 otherwise.
static inline uint8_t wz_find_entity_by_authored_id(
    const WzBehaviorFrameFacts* facts,
    const char* authored_id,
    WzBehaviorEntityId* out_entity)
{
    if (!facts || !facts->find_entity_by_authored_id) {
        return 0;
    }
    return facts->find_entity_by_authored_id(
        facts->scene_query_user,
        authored_id,
        out_entity);
}

// Find a descendant of `ancestor` by node name, scoped to `ancestor`'s subtree
// (instance-safe -- see WzFindBehaviorDescendantByNameFn).
static inline uint8_t wz_find_descendant_by_name(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId ancestor,
    const char* name,
    WzBehaviorEntityId* out_entity)
{
    if (!facts || !facts->find_descendant_by_name) {
        return 0;
    }
    return facts->find_descendant_by_name(
        facts->scene_query_user,
        ancestor,
        name,
        out_entity);
}

// Convenience: find a descendant of SELF (the bound entity) by name.
static inline uint8_t wz_self_find_descendant_by_name(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    const char* name,
    WzBehaviorEntityId* out_entity)
{
    return wz_find_descendant_by_name(
        facts, wz_self(event), name, out_entity);
}

static inline uint8_t wz_config_bool(
    const WzBehaviorFrameFacts* facts,
    const char* key,
    uint8_t* out_value)
{
    if (!facts || !facts->get_config_bool) {
        return 0;
    }
    return facts->get_config_bool(
        facts->behavior_config_user,
        key,
        out_value);
}

static inline uint8_t wz_config_number(
    const WzBehaviorFrameFacts* facts,
    const char* key,
    double* out_value)
{
    if (!facts || !facts->get_config_number) {
        return 0;
    }
    return facts->get_config_number(
        facts->behavior_config_user,
        key,
        out_value);
}

static inline uint8_t wz_config_float(
    const WzBehaviorFrameFacts* facts,
    const char* key,
    float* out_value)
{
    if (!out_value) {
        return 0;
    }

    double value = 0.0;
    if (!wz_config_number(facts, key, &value)) {
        return 0;
    }

    *out_value = (float)value;
    return 1;
}

static inline uint8_t wz_config_string(
    const WzBehaviorFrameFacts* facts,
    const char* key,
    char* out_buffer,
    uint32_t buffer_size,
    uint32_t* out_required_size)
{
    if (!facts || !facts->get_config_string) {
        return 0;
    }
    return facts->get_config_string(
        facts->behavior_config_user,
        key,
        out_buffer,
        buffer_size,
        out_required_size);
}

static inline uint8_t wz_find_entity_by_name(
    const WzBehaviorInitFacts* facts,
    const char* name,
    WzBehaviorEntityId* out_entity)
{
    if (!facts || !facts->find_entity_by_name) {
        return 0;
    }
    return facts->find_entity_by_name(
        facts->scene_query_user,
        name,
        out_entity);
}

static inline uint8_t wz_find_entity_by_authored_id(
    const WzBehaviorInitFacts* facts,
    const char* authored_id,
    WzBehaviorEntityId* out_entity)
{
    if (!facts || !facts->find_entity_by_authored_id) {
        return 0;
    }
    return facts->find_entity_by_authored_id(
        facts->scene_query_user,
        authored_id,
        out_entity);
}

// Find a descendant of `ancestor` by node name, scoped to `ancestor`'s subtree
// (instance-safe -- see WzFindBehaviorDescendantByNameFn). In on_init, pass the
// behavior's own entity (the init function's entity argument) as `ancestor` to
// resolve + cache a child handle, e.g. a spawned tank's grafted "turret".
static inline uint8_t wz_find_descendant_by_name(
    const WzBehaviorInitFacts* facts,
    WzBehaviorEntityId ancestor,
    const char* name,
    WzBehaviorEntityId* out_entity)
{
    if (!facts || !facts->find_descendant_by_name) {
        return 0;
    }
    return facts->find_descendant_by_name(
        facts->scene_query_user,
        ancestor,
        name,
        out_entity);
}

static inline uint8_t wz_config_bool(
    const WzBehaviorInitFacts* facts,
    const char* key,
    uint8_t* out_value)
{
    if (!facts || !facts->get_config_bool) {
        return 0;
    }
    return facts->get_config_bool(
        facts->behavior_config_user,
        key,
        out_value);
}

static inline uint8_t wz_config_number(
    const WzBehaviorInitFacts* facts,
    const char* key,
    double* out_value)
{
    if (!facts || !facts->get_config_number) {
        return 0;
    }
    return facts->get_config_number(
        facts->behavior_config_user,
        key,
        out_value);
}

static inline uint8_t wz_config_float(
    const WzBehaviorInitFacts* facts,
    const char* key,
    float* out_value)
{
    if (!out_value) {
        return 0;
    }

    double value = 0.0;
    if (!wz_config_number(facts, key, &value)) {
        return 0;
    }

    *out_value = (float)value;
    return 1;
}

static inline uint8_t wz_config_string(
    const WzBehaviorInitFacts* facts,
    const char* key,
    char* out_buffer,
    uint32_t buffer_size,
    uint32_t* out_required_size)
{
    if (!facts || !facts->get_config_string) {
        return 0;
    }
    return facts->get_config_string(
        facts->behavior_config_user,
        key,
        out_buffer,
        buffer_size,
        out_required_size);
}

static inline const char* wz_event_name(WzBehaviorEventKind kind)
{
    switch (kind) {
    case WZ_EVENT_FRAME_UPDATE:
        return "frame.update";
    case WZ_EVENT_SCENE_LOADED:
        return "scene.loaded";
    case WZ_EVENT_SELF_START:
        return "self.start";
    case WZ_EVENT_COGNITION_TICK:
        return "cognition.tick";
    case WZ_EVENT_COLLISION_ENTER:
        return "collision.enter";
    case WZ_EVENT_COLLISION_STAY:
        return "collision.stay";
    case WZ_EVENT_COLLISION_EXIT:
        return "collision.exit";
    case WZ_EVENT_PROXIMITY_ENTER:
        return "proximity.enter";
    case WZ_EVENT_PROXIMITY_STAY:
        return "proximity.stay";
    case WZ_EVENT_PROXIMITY_EXIT:
        return "proximity.exit";
    case WZ_EVENT_INPUT_KEY_PRESSED:
        return "input.key.pressed";
    case WZ_EVENT_INPUT_KEY_RELEASED:
        return "input.key.released";
    case WZ_EVENT_INPUT_MOUSE_BUTTON_PRESSED:
        return "input.mouse_button.pressed";
    case WZ_EVENT_INPUT_MOUSE_BUTTON_RELEASED:
        return "input.mouse_button.released";
    case WZ_EVENT_INPUT_CONTROLLER_BUTTON_PRESSED:
        return "input.controller_button.pressed";
    case WZ_EVENT_INPUT_CONTROLLER_BUTTON_RELEASED:
        return "input.controller_button.released";
    case WZ_EVENT_INPUT_CONTROLLER_AXIS_CHANGED:
        return "input.controller_axis.changed";
    case WZ_EVENT_GPU_COMPUTE_REQUEST:
        return "gpu.compute.request";
    case WZ_EVENT_GPU_COMPUTE_COMPLETED:
        return "gpu.compute.completed";
    case WZ_EVENT_GPU_COMPUTE_FAILED:
        return "gpu.compute.failed";
    default:
        return "unknown";
    }
}
