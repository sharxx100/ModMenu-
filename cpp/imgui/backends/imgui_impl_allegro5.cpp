// dear imgui: Renderer + Platform Backend for Allegro 5
// (Info: Allegro 5 is a cross-platform general purpose library for handling windows, inputs, graphics, etc.)

// Implemented features:
//  [X] Renderer: User texture binding. Use 'ALLEGRO_BITMAP*' as texture identifier. Read the FAQ about ImTextureID/ImTextureRef!
//  [X] Renderer: Texture updates support for dynamic font atlas (ImGuiBackendFlags_RendererHasTextures).
//  [X] Platform: Keyboard support. Since 1.87 we are using the io.AddKeyEvent() function. Pass ImGuiKey values to all key functions e.g. ImGui::IsKeyPressed(ImGuiKey_Space). [Legacy ALLEGRO_KEY_* values are obsolete since 1.87 and not supported since 1.91.5]
//  [X] Platform: Clipboard support (from Allegro 5.1.12).
//  [X] Platform: Mouse cursor shape and visibility (ImGuiBackendFlags_HasMouseCursors). Disable with 'io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange'.
// Missing features or Issues:
//  [ ] Renderer: The renderer is suboptimal as we need to unindex our buffers and convert vertices manually.
//  [ ] Renderer: Missing support for DrawCallback_SetSamplerLinear, DrawCallback_SetSamplerNearest callbacks: Allegro5 cannot enable/disable LINEAR bitmap flags after creation.
//  [ ] Platform: Missing gamepad support.
//  [ ] Renderer: Multi-viewport support (multiple windows).

// You can use unmodified imgui_impl_* files in your project. See examples/ folder for examples of using this.
// Prefer including the entire imgui/ repository into your project (either as a copy or as a submodule), and only build the backends you need.
// Learn about Dear ImGui:
// - FAQ                  https://dearimgui.com/faq
// - Getting Started      https://dearimgui.com/getting-started
// - Documentation        https://dearimgui.com/docs (same as your local docs/ folder).
// - Introduction, links and more at the top of imgui.cpp

// CHANGELOG
// (minor and older changes stripped away, please see git history for details)
//  2026-04-23: Added support for standard draw callbacks (in platform_io): DrawCallback_ResetRenderState (others cannot be supported by Allegro5). (#9378)
//  2025-09-18: Call platform_io.ClearRendererHandlers() and platform_io.ClearPlatformHandlers() on shutdown.
//  2025-08-12: Inputs: fixed missing support for ImGuiKey_PrintScreen under Windows, as raw Allegro 5 does not receive it.
//  2025-08-12: Added ImGui_ImplAllegro5_SetDisplay() function to change current ALLEGRO_DISPLAY, as Allegro applications often need to do that.
//  2025-07-07: Fixed texture update broken on some platforms where ALLEGRO_LOCK_WRITEONLY needed all texels to be rewritten.
//  2025-06-11: Added support for ImGuiBackendFlags_RendererHasTextures, for dynamic font atlas. Removed ImGui_ImplSDLGPU3_CreateFontsTexture() and ImGui_ImplSDLGPU3_DestroyFontsTexture().
//  2025-02-18: Added ImGuiMouseCursor_Wait and ImGuiMouseCursor_Progress mouse cursor support.
//  2025-01-06: Avoid calling al_set_mouse_cursor() repeatedly since it appears to leak on on X11 (#8256).
//  2024-08-22: moved some OS/backend related function pointers from ImGuiIO to ImGuiPlatformIO:
//               - io.GetClipboardTextFn    -> platform_io.Platform_GetClipboardTextFn
//               - io.SetClipboardTextFn    -> platform_io.Platform_SetClipboardTextFn
//  2022-11-30: Renderer: Restoring using al_draw_indexed_prim() when Allegro version is >= 5.2.5.
//  2022-10-11: Using 'nullptr' instead of 'NULL' as per our switch to C++11.
//  2022-09-26: Inputs: Renamed ImGuiKey_ModXXX introduced in 1.87 to ImGuiMod_XXX (old names still supported).
//  2022-01-26: Inputs: replaced short-lived io.AddKeyModsEvent() (added two weeks ago) with io.AddKeyEvent() using ImGuiKey_ModXXX flags. Sorry for the confusion.
//  2022-01-17: Inputs: calling new io.AddMousePosEvent(), io.AddMouseButtonEvent(), io.AddMouseWheelEvent() API (1.87+).
//  2022-01-17: Inputs: always calling io.AddKeyModsEvent() next and before key event (not in NewFrame) to fix input queue with very low framerates.
//  2022-01-10: Inputs: calling new io.AddKeyEvent(), io.AddKeyModsEvent() + io.SetKeyEventNativeData() API (1.87+). Support for full ImGuiKey range.
//  2021-12-08: Renderer: Fixed mishandling of the ImDrawCmd::IdxOffset field! This is an old bug but it never had an effect until some internal rendering changes in 1.86.
//  2021-08-17: Calling io.AddFocusEvent() on ALLEGRO_EVENT_DISPLAY_SWITCH_OUT/ALLEGRO_EVENT_DISPLAY_SWITCH_IN events.
//  2021-06-29: Reorganized backend to pull data from a single structure to facilitate usage with multiple-contexts (all g_XXXX access changed to bd->XXXX).
//  2021-05-19: Renderer: Replaced direct access to ImDrawCmd::TextureId with a call to ImDrawCmd::GetTexID(). (will become a requirement)
//  2021-02-18: Change blending equation to preserve alpha in output buffer.
//  2020-08-10: Inputs: Fixed horizontal mouse wheel direction.
//  2019-12-05: Inputs: Added support for ImGuiMouseCursor_NotAllowed mouse cursor.
//  2019-07-21: Inputs: Added mapping for ImGuiKey_KeyPadEnter.
//  2019-05-11: Inputs: Don't filter character value from ALLEGRO_EVENT_KEY_CHAR before calling AddInputCharacter().
//  2019-04-30: Renderer: Added support for special ImDrawCallback_ResetRenderState callback to reset render state.
//  2018-11-30: Platform: Added touchscreen support.
//  2018-11-30: Misc: Setting up io.BackendPlatformName/io.BackendRendererName so they can be displayed in the About Window.
//  2018-06-13: Platform: Added clipboard support (from Allegro 5.1.12).
//  2018-06-13: Renderer: Use draw_data->DisplayPos and draw_data->DisplaySize to setup projection matrix and clipping rectangle.
//  2018-06-13: Renderer: Stopped using al_draw_indexed_prim() as it is buggy in Allegro's DX9 backend.
//  2018-06-13: Renderer: Backup/restore transform and clipping rectangle.
//  2018-06-11: Misc: Setup io.BackendFlags ImGuiBackendFlags_HasMouseCursors flag + honor ImGuiConfigFlags_NoMouseCursorChange flag.
//  2018-04-18: Misc: Renamed file from imgui_impl_a5.cpp to imgui_impl_allegro5.cpp.
//  2018-04-18: Misc: Added support for 32-bit vertex indices to avoid conversion at runtime. Added imconfig_allegro5.h to enforce 32-bit indices when included from imgui.h.
//  2018-02-16: Misc: Obsoleted the io.RenderDrawListsFn callback and exposed ImGui_ImplAllegro5_RenderDrawData() in the .h file so you can call it yourself.
//  2018-02-06: Misc: Removed call to ImGui::Shutdown() which is not available from 1.60 WIP, user needs to call CreateContext/DestroyContext themselves.
//  2018-02-06: Inputs: Added mapping for ImGuiKey_Space.

#include "imgui.h"
#ifndef IMGUI_DISABLE
#include "imgui_impl_allegro5.h"
#include <stdint.h>     // uint64_t
#include <cstring>      // memcpy

// Allegro
#include <allegro5/allegro.h>
#include <allegro5/allegro_primitives.h>
#ifdef _WIN32
#include <allegro5/allegro_windows.h>
#endif
#define ALLEGRO_HAS_CLIPBOARD           ((ALLEGRO_VERSION_INT & ~ALLEGRO_UNSTABLE_BIT) >= ((5 << 24) | (1 << 16) | (12 << 8))) // Clipboard only supported from Allegro 5.1.12
#define ALLEGRO_HAS_DRAW_INDEXED_PRIM   ((ALLEGRO_VERSION_INT & ~ALLEGRO_UNSTABLE_BIT) >= ((5 << 24) | (2 << 16) | ( 5 << 8))) // DX9 implementation of al_draw_indexed_prim() got fixed in Allegro 5.2.5

// Visual Studio warnings
#ifdef _MSC_VER
#pragma warning (disable: 4127) // condition expression is constant
#endif

struct ImDrawVertAllegro
{
    ImVec2          pos;
    ImVec2          uv;
    ALLEGRO_COLOR   col;
};

// FIXME-OPT: Unfortunately Allegro doesn't support 32-bit packed colors so we have to convert them to 4 float as well..
// FIXME-OPT: Consider inlining al_map_rgba()?
// see https://github.com/liballeg/allegro5/blob/master/src/pixels.c#L554
// and https://github.com/liballeg/allegro5/blob/master/include/allegro5/internal/aintern_pixels.h
#define DRAW_VERT_IMGUI_TO_ALLEGRO(DST, SRC)  { (DST)->pos = (SRC)->pos; (DST)->uv = (SRC)->uv; unsigned char* c = (unsigned char*)&(SRC)->col; (DST)->col = al_map_rgba(c[0], c[1], c[2], c[3]); }

// Allegro Data
struct ImGui_ImplAllegro5_Data
{
    ALLEGRO_DISPLAY*            Display;
    ALLEGRO_BITMAP*             Texture;
    double                      Time;
    ALLEGRO_MOUSE_CURSOR*       MouseCursorInvisible;
    ALLEGRO_VERTEX_DECL*        VertexDecl;
    char*                       ClipboardTextData;
    ImGuiMouseCursor            LastCursor;

    ImVector<ImDrawVertAllegro> BufVertices;
    ImVector<int>               BufIndices;

    ImGui_ImplAllegro5_Data()   { memset((void*)this, 0, sizeof(*this)); }
};

// Backend data stored in io.BackendPlatformUserData to allow support for multiple Dear ImGui contexts
// It is STRONGLY preferred that you use docking branch with multi-viewports (== single Dear ImGui context + multiple windows) instead of multiple Dear ImGui contexts.
// FIXME: multi-context support is not well tested and probably dysfunctional in this backend.
static ImGui_ImplAllegro5_Data* ImGui_ImplAllegro5_GetBackendData()     { return ImGui::GetCurrentContext() ? (ImGui_ImplAllegro5_Data*)ImGui::GetIO().BackendPlatformUserData : nullptr; }

static void ImGui_ImplAllegro5_SetupRenderState(ImDrawData* draw_data)
{
    // Setup blending
    al_set_separate_blender(ALLEGRO_ADD, ALLEGRO_ALPHA, ALLEGRO_INVERSE_ALPHA, ALLEGRO_ADD, ALLEGRO_ONE, ALLEGRO_INVERSE_ALPHA);

    // Setup orthographic projection matrix
    // Our visible imgui space lies from draw_data->DisplayPos (top left) to draw_data->DisplayPos+data_data->DisplaySize (bottom right).
    {
        float L = draw_data->DisplayPos.x;
        float R = draw_data->DisplayPos.x + draw_data->DisplaySize.x;
        float T = draw_data->DisplayPos.y;
        float B = draw_data->DisplayPos.y + draw_data->DisplaySize.y;
        ALLEGRO_TRANSFORM transform;
        al_identity_transform(&transform);
        al_use_transform(&transform);
        al_orthographic_transform(&transform, L, T, 1.0f, R, B, -1.0f);
        al_use_projection_transform(&transform);
    }
}

// Draw callbacks
static void ImGui_ImplAllegro5_DrawCallback_ResetRenderState(const ImDrawList*, const ImDrawCmd*) {} // Intentionally empty. Used as an identifier for rendering loop to call its code. Simpler to implement this way.

// Render function
void ImGui_ImplAllegro5_RenderDrawData(ImDrawData* draw_data)
{
    // Avoid rendering when minimized
    if (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f)
        return;

    // Catch up with texture updates. Most of the times, the list will have 1 element with an OK status, aka nothing to do.
    // (This almost always points to ImGui::GetPlatformIO().Textures[] but is part of ImDrawData to allow overriding or disabling texture updates).
    if (draw_data->Textures != nullptr)
        for (ImTextureData* tex : *draw_data->Textures)
            if (tex->Status != ImTextureStatus_OK)
                ImGui_ImplAllegro5_UpdateTexture(tex);

    // Backup Allegro state that will be modified
    ImGui_ImplAllegro5_Data* bd = ImGui_ImplAllegro5_GetBackendData();
    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
    ALLEGRO_TRANSFORM last_transform = *al_get_current_transform();
    ALLEGRO_TRANSFORM last_projection_transform = *al_get_current_projection_transform();
    int last_clip_x, last_clip_y, last_clip_w, last_clip_h;
    al_get_clipping_rectangle(&last_clip_x, &last_clip_y, &last_clip_w, &last_clip_h);
    int last_blender_op, last_blender_src, last_blender_dst;
    al_get_blender(&last_blender_op, &last_blender_src, &last_blender_dst);

    // Setup desired render state
    ImGui_ImplAllegro5_SetupRenderState(draw_data);

    // Render command lists
    for (const ImDrawList* draw_list : draw_data->CmdLists)
    {
        ImVector<ImDrawVertAllegro>& vertices = bd->BufVertices;
#if ALLEGRO_HAS_DRAW_INDEXED_PRIM
        vertices.resize(draw_list->VtxBuffer.Size);
        for (int i = 0; i < draw_list->VtxBuffer.Size; i++)
        {
            const ImDrawVert* src_v = &draw_list->VtxBuffer[i];
            ImDrawVertAllegro* dst_v = &vertices[i];
            DRAW_VERT_IMGUI_TO_ALLEGRO(dst_v, src_v);
        }
        const int* indices = nullptr;
        if (sizeof(ImDrawIdx) == 2)
        {
            // FIXME-OPT: Allegro doesn't support 16-bit indices.
            // You can '#define ImDrawIdx int' in imconfig.h to request Dear ImGui to output 32-bit indices.
            // Otherwise, we convert them from 16-bit to 32-bit at runtime here, which works perfectly but is a little wasteful.
            bd->BufIndices.resize(draw_list->IdxBuffer.Size);
            for (int i = 0; i < draw_list->IdxBuffer.Size; ++i)
                bd->BufIndices[i] = (int)draw_list->IdxBuffer.Data[i];
            indices = bd->BufIndices.Data;
        }
        else if (sizeof(ImDrawIdx) == 4)
        {
            indices = (const int*)draw_list->IdxBuffer.Data;
        }
#else
        // Allegro's implementation of al_draw_indexed_prim() for DX9 was broken until 5.2.5. Unindex buffers ourselves while converting vertex format.
        vertices.resize(draw_list->IdxBuffer.Size);
        for (int i = 0; i < draw_list->IdxBuffer.Size; i++)
        {
            const ImDrawVert* src_v = &draw_list->VtxBuffer[draw_list->IdxBuffer[i]];
            ImDrawVertAllegro* dst_v = &vertices[i];
            DRAW_VERT_IMGUI_TO_ALLEGRO(dst_v, src_v);
        }
#endif

        // Render command lists
        ImVec2 clip_off = draw_data->DisplayPos;
        for (int cmd_i = 0; cmd_i < draw_list->CmdBuffer.Size; cmd_i++)
        {
            const ImDrawCmd* pcmd = &draw_list->CmdBuffer[cmd_i];
            if (pcmd->UserCallback)
            {
                // User callback, registered via ImDrawList::AddCallback()
                if (pcmd->UserCallback == ImGui_ImplAllegro5_DrawCallback_ResetRenderState)
                    ImGui_ImplAllegro5_SetupRenderState(draw_data);
                else
                    pcmd->UserCallback(draw_list, pcmd);
            }
            else
            {
                // Project scissor/clipping rectangles into framebuffer space
                ImVec2 clip_min(pcmd->ClipRect.x - clip_off.x, pcmd->ClipRect.y - clip_off.y);
                ImVec2 clip_max(pcmd->ClipRect.z - clip_off.x, pcmd->ClipRect.w - clip_off.y);
                if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y)
                    continue;

                // Apply scissor/clipping rectangle, Draw
                ALLEGRO_BITMAP* texture = (ALLEGRO_BITMAP*)pcmd->GetTexID();
                al_set_clipping_rectangle(clip_min.x, clip_min.y, clip_max.x - clip_min.x, clip_max.y - clip_min.y);
#if ALLEGRO_HAS_DRAW_INDEXED_PRIM
                al_draw_indexed_prim(&vertices[0], bd->VertexDecl, texture, &indices[pcmd->IdxOffset], pcmd->ElemCount, ALLEGRO_PRIM_TRIANGLE_LIST);
#else
                al_draw_prim(&vertices[0], bd->VertexDecl, texture, pcmd->IdxOffset, pcmd->IdxOffset + pcmd->ElemCount, ALLEGRO_PRIM_TRIANGLE_LIST);
#endif
            }
        }
    }

    // Restore modified Allegro state
    al_set_blender(last_blender_op, last_blender_src, last_blender_dst);
    al_set_clipping_rectangle(last_clip_x, last_clip_y, last_clip_w, last_clip_h);
    al_use_transform(&last_transform);
    al_use_projection_transform(&last_projection_transform);
}

bool ImGui_ImplAllegro5_CreateDeviceObjects()
{
    ImGui_ImplAllegro5_Data* bd = ImGui_ImplAllegro5_GetBackendData();

    // Create an invisible mouse cursor
    // Because al_hide_mouse_cursor() seems to mess up with the actual inputs..
    ALLEGRO_BITMAP* mouse_cursor = al_create_bitmap(8, 8);
    bd->MouseCursorInvisible = al_create_mouse_cursor(mouse_cursor, 0, 0);
    al_destroy_bitmap(mouse_cursor);

    return true;
}

void ImGui_ImplAllegro5_UpdateTexture(ImTextureData* tex)
{
    if (tex->Status == ImTextureStatus_WantCreate)
    {
        // Create and upload new texture to graphics system
        //IMGUI_DEBUG_LOG("UpdateTexture #%03d: WantCreate %dx%d\n", tex->UniqueID, tex->Width, tex->Height);
        IM_ASSERT(tex->TexID == ImTextureID_Invalid && tex->BackendUserData == nullptr);
        IM_ASSERT(tex->Format == ImTextureFormat_RGBA32);

        // Create texture (bilinear sampling is required)
        const int new_bitmap_flags = al_get_new_bitmap_flags();
        int new_bitmap_format = al_get_new_bitmap_format();
        al_set_new_bitmap_flags(ALLEGRO_MEMORY_BITMAP | ALLEGRO_MIN_LINEAR | ALLEGRO_MAG_LINEAR);
        al_set_new_bitmap_format(ALLEGRO_PIXEL_FORMAT_ABGR_8888_LE);
        ALLEGRO_BITMAP* cpu_bitmap = al_create_bitmap(tex->Width, tex->Height);
        IM_ASSERT(cpu_bitmap != nullptr && "Backend failed to create texture!");

        // Upload pixels
        ALLEGRO_LOCKED_REGION* locked_region = al_lock_bitmap(cpu_bitmap, al_get_bitmap_format(cpu_bitmap), ALLEGRO_LOCK_WRITEONLY);
        IM_ASSERT(locked_region != nullptr && "Backend failed to create texture!");
        memcpy(locked_region->data, tex->GetPixels(), tex->GetSizeInBytes());
        al_unlock_bitmap(cpu_bitmap);

        // Convert software texture to hardware texture.
        al_set_new_bitmap_flags(ALLEGRO_VIDEO_BITMAP);
        al_set_new_bitmap_format(ALLEGRO_PIXEL_FORMAT_ANY_32_WITH_ALPHA);
        ALLEGRO_BITMAP* gpu_bitmap = al_clone_bitmap(cpu_bitmap);
        al_destroy_bitmap(cpu_bitmap);
        IM_ASSERT(gpu_bitmap != nullptr && "Backend failed to create texture!");

        al_set_new_bitmap_flags(new_bitmap_flags);
        al_set_new_bitmap_format(new_bitmap_format);

        // Store identifiers
        tex->SetTexID((ImTextureID)(intptr_t)gpu_bitmap);
        tex->SetStatus(ImTextureStatus_OK);
    }
    else if (tex->Status == ImTextureStatus_WantUpdates)
    {
        // Update selected blocks. We only ever write to textures regions which have never been used before!
        // This backend choose to use tex->Updates[] but you can use tex->UpdateRect to upload a single region.
        ImTextureRect r = tex->UpdateRect; // Bounding box encompassing all individual updates
        ALLEGRO_BITMAP* gpu_bitmap = (ALLEGRO_BITMAP*)(intptr_t)tex->TexID;
        ALLEGRO_LOCKED_REGION* locked_region = al_lock_bitmap_region(gpu_bitmap, r.x, r.y, r.w, r.h, al_get_bitmap_format(gpu_bitmap), ALLEGRO_LOCK_WRITEONLY);
        IM_ASSERT(locked_region && "Backend failed to update texture!");
        for (int y = 0; y < r.h; y++)
            memcpy((unsigned char*)locked_region->data + locked_region->pitch * y, tex->GetPixelsAt(r.x, r.y + y), r.w * tex->BytesPerPixel); // dst, src, block pitch
        al_unlock_bitmap(gpu_bitmap);
        tex->SetStatus(ImTextureStatus_OK);
    }
    else if (tex->Status == ImTextureStatus_WantDestroy)
    {
        ALLEGRO_BITMAP* backend_tex = (ALLEGRO_BITMAP*)(intptr_t)tex->TexID;
        if (backend_tex)
            al_destroy_bitmap(backend_tex);

        // Clear identifiers and mark as destroyed (in order to allow e.g. calling InvalidateDeviceObjects while running)
        tex->SetTexID(ImTextureID_Invalid);
        tex->SetStatus(ImTextureStatus_Destroyed);
    }
}

void ImGui_ImplAllegro5_InvalidateDeviceObjects()
{
    ImGui_ImplAllegro5_Data* bd = ImGui_ImplAllegro5_GetBackendData();

    // Destroy all textures
    for (ImTextureData* tex : ImGui::GetPlatformIO().Textures)
        if (tex->RefCount == 1)
        {
            tex->SetStatus(ImTextureStatus_WantDestroy);
            ImGui_ImplAllegro5_UpdateTexture(tex);
        }

    // Destroy mouse cursor
    if (bd->MouseCursorInvisible)
    {
        al_destroy_mouse_cursor(bd->MouseCursorInvisible);
        bd->MouseCursorInvisible = nullptr;
    }
}

#if ALLEGRO_HAS_CLIPBOARD
static const char* ImGui_ImplAllegro5_GetClipboardText(ImGuiContext*)
{
    ImGui_ImplAllegro5_Data* bd = ImGui_ImplAllegro5_GetBackendData();
    if (bd->ClipboardTextData)
        al_free(bd->ClipboardTextData);
    bd->ClipboardTextData = al_get_clipboard_text(bd->Display);
    return bd->ClipboardTextData;
}

static void ImGui_
