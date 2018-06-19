#include "vr-interaction.hpp"
#include "system-transform.hpp"
#include "system-identifier.hpp"
#include "system-render.hpp"
#include "system-collision.hpp"

/////////////////////////////////////////
//   vr_imgui_surface implementation   //
/////////////////////////////////////////

vr_imgui_surface::vr_imgui_surface(entity_orchestrator * orch, environment * env, const uint2 size, GLFWwindow * window) 
    : imgui_surface(size, window)
{
    // Setup the billboard entity
    auto mesh = make_fullscreen_quad_ndc_geom();
    for (auto & v : mesh.vertices) { v *= 0.15f; }

    create_handle_for_asset("imgui-billboard", make_mesh_from_geometry(mesh)); // gpu mesh
    create_handle_for_asset("imgui-billboard", std::move(mesh)); // cpu mesh

    imgui_billboard = env->track_entity(orch->create_entity());
    env->identifier_system->create(imgui_billboard, "imgui-billboard");
    env->xform_system->create(imgui_billboard, transform(float3(0, 0, 0)), { 1.f, 1.f, 1.f });
    env->render_system->create(imgui_billboard, material_component(imgui_billboard, material_handle("imgui")));
    env->render_system->create(imgui_billboard, mesh_component(imgui_billboard, gpu_mesh_handle("imgui-billboard")));
    env->collision_system->create(imgui_billboard, geometry_component(imgui_billboard, cpu_mesh_handle("imgui-billboard")));

    imgui_material = std::make_shared<polymer_fx_material>();
    imgui_material->shader = shader_handle("textured");
    env->mat_library->create_material("imgui", imgui_material);
}

void vr_imgui_surface::update(environment * env, const transform & pointer_transform, const transform & billboard_origin, bool trigger_state)
{
    /*
    // Update billboard position
    if (auto * tc = env->xform_system->get_local_transform(pointer))
    {
        env->xform_system->set_local_transform(imgui_billboard, billboard_origin);
    }

    const uint2 framebuffer_size = get_size();
    const float2 pixel_coord = { (1 - result.r.uv.x) * framebuffer_size.x, result.r.uv.y * framebuffer_size.y };

    app_input_event controller_event;
    controller_event.type = app_input_event::MOUSE;
    controller_event.action = trigger_state;
    controller_event.value = { 0, 0 };
    controller_event.cursor = pixel_coord;
    imgui->update_input(controller_event);
    */
}

std::vector<entity> vr_imgui_surface::get_renderables() const
{
    return { imgui_billboard };
}

void vr_imgui_surface::update_renderloop()
{
    imgui_material->use();
    auto & imgui_shader = imgui_material->compiled_shader->shader;
    imgui_shader.texture("s_texture", 0, get_render_texture(), GL_TEXTURE_2D);
    imgui_shader.unbind();
}

/////////////////////////////////
//   vr_gizmo implementation   //
/////////////////////////////////

vr_gizmo::vr_gizmo(entity_orchestrator * orch, environment * env, openvr_hmd * hmd, vr_input_processor * processor)
    : env(env), hmd(hmd), processor(processor)
{
    auto unlit_material = std::make_shared<polymer_fx_material>();
    unlit_material->shader = shader_handle("unlit-vertex-color");
    env->mat_library->create_material("unlit-vertex-color", unlit_material);

    gizmo_entity = env->track_entity(orch->create_entity());
    env->identifier_system->create(gizmo_entity, "gizmo-renderable");
    env->xform_system->create(gizmo_entity, transform(float3(0, 0, 0)), { 1.f, 1.f, 1.f });
    env->render_system->create(gizmo_entity, material_component(gizmo_entity, material_handle("unlit-vertex-color")));
    env->render_system->create(gizmo_entity, mesh_component(gizmo_entity));
    env->collision_system->create(gizmo_entity, geometry_component(gizmo_entity));

    gizmo_ctx.render = [this, env](const tinygizmo::geometry_mesh & r)
    {
        const std::vector<tinygizmo::geometry_vertex> & verts = r.vertices;
        const std::vector<linalg::aliases::uint3> & tris = reinterpret_cast<const std::vector<linalg::aliases::uint3> &>(r.triangles);

        // For rendering
        if (auto * mc = env->render_system->get_mesh_component(gizmo_entity))
        {
            auto & gizmo_gpu_mesh = mc->mesh.get();

            // Upload new gizmo mesh data to the gpu
            gizmo_gpu_mesh.set_vertices(verts, GL_DYNAMIC_DRAW);
            gizmo_gpu_mesh.set_attribute(0, 3, GL_FLOAT, GL_FALSE, sizeof(tinygizmo::geometry_vertex), (GLvoid*)offsetof(tinygizmo::geometry_vertex, position));
            gizmo_gpu_mesh.set_attribute(1, 3, GL_FLOAT, GL_FALSE, sizeof(tinygizmo::geometry_vertex), (GLvoid*)offsetof(tinygizmo::geometry_vertex, normal));
            gizmo_gpu_mesh.set_attribute(2, 3, GL_FLOAT, GL_FALSE, sizeof(tinygizmo::geometry_vertex), (GLvoid*)offsetof(tinygizmo::geometry_vertex, color));
            gizmo_gpu_mesh.set_elements(tris, GL_DYNAMIC_DRAW);
        }

        // For focus/defocus
        if (auto * gc = env->collision_system->get_component(gizmo_entity))
        {
            auto & gizmo_cpu_mesh = gc->geom.get();
            gizmo_cpu_mesh = make_cube();
        }
    };
}

void vr_gizmo::handle_event(const vr_input_event & event)
{
    if (event.type == vr_event_t::focus_begin && event.focus.result.e == gizmo_entity)
    {
        focused = true;
        std::cout << "vr_gizmo handle <<focus_begin>> event " << event.timestamp << std::endl;
    }
    else if (event.type == vr_event_t::focus_end && event.focus.result.e == gizmo_entity)
    {
        focused = false;
        std::cout << "vr_gizmo handle <<focus_end>> event " << event.timestamp << std::endl;
    }
}

void vr_gizmo::process(const float dt)
{
    const auto view = view_data(0, hmd->get_eye_pose(vr::Hmd_Eye::Eye_Left), hmd->get_proj_matrix(vr::Hmd_Eye::Eye_Left, 0.075f, 64.f));
    const auto vfov = vfov_from_projection(view.projectionMatrix);

    gizmo_state.cam.near_clip = view.nearClip;
    gizmo_state.cam.far_clip = view.farClip;
    gizmo_state.cam.yfov = vfov;
    gizmo_state.cam.position = minalg::float3(view.pose.position.x, view.pose.position.y, view.pose.position.z);
    gizmo_state.cam.orientation = minalg::float4(view.pose.orientation.x, view.pose.orientation.y, view.pose.orientation.z, view.pose.orientation.w);

    if (focused)
    {   
        const vr_input_focus focus = processor->get_focus();
        gizmo_state.ray_origin = minalg::float3(focus.r.origin.x, focus.r.origin.y, focus.r.origin.z);
        gizmo_state.ray_direction = minalg::float3(focus.r.direction.x, focus.r.direction.y, focus.r.direction.z);
    }

    // Draw gizmo @ transform
    tinygizmo::transform_gizmo("vr-gizmo", gizmo_ctx, xform);

    // Trigger render callback
    gizmo_ctx.draw();

    // Update transform
    if (auto * tc = env->xform_system->get_local_transform(gizmo_entity))
    {
        transform t(float3(xform.position.x, xform.position.y, xform.position.z));
        env->xform_system->set_local_transform(gizmo_entity, t);
    }
}

std::vector<entity> vr_gizmo::get_renderables() const
{
    return { gizmo_entity };
}