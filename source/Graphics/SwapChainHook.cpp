#include "SwapChainHook.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <algorithm>
#include <android/native_window.h>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>

#ifndef VK_USE_PLATFORM_ANDROID_KHR
#define VK_USE_PLATFORM_ANDROID_KHR
#endif
#include <vulkan/vulkan.h>

#include "AndroidPlatform/AndroidPlatform.h"
#include "ImGui/ImGuiSetup.h"
#include "Resource/ResourceManager.h"
#include "Dobby/dobby.h"
#include "ImGuiExtern/ImGuiSoftKeyboard.h"
#include "Utils/Logger.h"
#include "imgui/backends/imgui_impl_android.h"
#include "imgui/backends/imgui_impl_opengl3.h"
#include "imgui/backends/imgui_impl_vulkan.h"
#include "imgui/imgui.h"

// ===========================================================================
// Set to 0 to disable detailed SwapChainHook diagnostics; LOGE remains enabled.
// ===========================================================================
#define SWAPCHAIN_DEBUG_LOG 1

#if SWAPCHAIN_DEBUG_LOG
#define SCH_LOGI(fmt, ...) LOGI(fmt, ##__VA_ARGS__)
#define SCH_LOGW(fmt, ...) LOGW(fmt, ##__VA_ARGS__)
#else
#define SCH_LOGI(fmt, ...) do {} while(0)
#define SCH_LOGW(fmt, ...) do {} while(0)
#endif

// ===========================================================================
// Internal state.
// ===========================================================================

static std::atomic<bool>           g_Installed{false};
static std::atomic<bool>           g_ImGuiReady{false};
static std::mutex                  g_CallbackMutex;
static std::function<void()>       g_RenderCallback;
static int                         g_Width  = 0;
static int                         g_Height = 0;
static bool                        g_VkInitialized = false;

// ---------------------------------------------------------------------------
// OpenGL ES path.
// ---------------------------------------------------------------------------

static EGLBoolean (*g_OrigEglSwapBuffers)(EGLDisplay, EGLSurface) = nullptr;
static PFNEGLSWAPBUFFERSWITHDAMAGEKHRPROC g_OrigEglSwapBuffersWithDamage = nullptr;
static bool g_GlesInitialized = false;
static ANativeWindow* g_ImGuiWindow = nullptr;
static EGLContext g_ImGuiEglContext = EGL_NO_CONTEXT;

/**
 * Initialize ImGui using the host EGL context on first use.
 */
static bool InitImGuiOnGameContext(EGLDisplay display, EGLSurface surface)
{
    // Query the surface dimensions.
    EGLint w = 0, h = 0;
    eglQuerySurface(display, surface, EGL_WIDTH, &w);
    eglQuerySurface(display, surface, EGL_HEIGHT, &h);
    SCH_LOGI("[SwapChainHook] eglQuerySurface returned %dx%d", w, h);

    // Use ANativeWindow dimensions as fallback.
    ANativeWindow* nativeWindow = AndroidPlatform::GetNativeWindow();
    if ((w <= 0 || h <= 0) && nativeWindow)
    {
        w = ANativeWindow_getWidth(nativeWindow);
        h = ANativeWindow_getHeight(nativeWindow);
        SCH_LOGI("[SwapChainHook] Fallback to ANativeWindow size %dx%d", w, h);
    }

    if (w <= 0 || h <= 0)
    {
        LOGE("[SwapChainHook] Invalid surface size %dx%d", w, h);
        return false;
    }
    g_Width = w;
    g_Height = h;

    ANativeWindow* initWindow = AndroidPlatform::GetNativeWindow();
    if (!initWindow)
    {
        LOGE("[SwapChainHook] ANativeWindow is null, cannot init ImGui");
        return false;
    }

    ImGuiSetup::EnsureContext(initWindow);
    ImGui::GetIO().DisplaySize = ImVec2((float)w, (float)h);

    // Reuse the host OpenGL context.
    ImGui_ImplOpenGL3_Init("#version 300 es");

    // Fonts.
    ResourceManager::GetInstance().initializeFonts(30.0f);

    g_GlesInitialized = true;
    g_ImGuiReady.store(true);
    g_ImGuiWindow = initWindow;
    g_ImGuiEglContext = eglGetCurrentContext();

    SCH_LOGI("[SwapChainHook] ImGui initialized on game EGL context=%p  %dx%d", g_ImGuiEglContext, w, h);
    return true;
}

/**
 * eglSwapBuffers hook: save the GL state handled below, render ImGui into framebuffer 0, restore
 * that state and call the original function. Complete host-state preservation requires runtime
 * validation.
 *
 *
 *
 *
 */
static EGLBoolean Hooked_eglSwapBuffers(EGLDisplay display, EGLSurface surface)
{
    if (eglGetCurrentContext() == EGL_NO_CONTEXT)
        return g_OrigEglSwapBuffers(display, surface);

    // Skip EGL rendering while Vulkan owns ImGui rendering.
    if (g_VkInitialized)
        return g_OrigEglSwapBuffers(display, surface);

    if (!g_GlesInitialized)
    {
        if (!InitImGuiOnGameContext(display, surface))
            return g_OrigEglSwapBuffers(display, surface);
    }

    ANativeWindow* currentWindow = AndroidPlatform::GetNativeWindow();
    if (!currentWindow)
        return g_OrigEglSwapBuffers(display, surface);

    // Reinitialize when ANativeWindow or EGL context changes.
    EGLContext currentCtx = eglGetCurrentContext();
    if (g_GlesInitialized && (currentWindow != g_ImGuiWindow || currentCtx != g_ImGuiEglContext))
    {
        SCH_LOGI("[SwapChainHook] EGL changed: window %p->%p  ctx %p->%p, reinitializing ImGui",
                 g_ImGuiWindow, currentWindow, g_ImGuiEglContext, currentCtx);
        g_ImGuiReady.store(false);
        ImGui_ImplOpenGL3_Shutdown();
        ImGuiSetup::TeardownContext();
        g_GlesInitialized = false;
        g_ImGuiWindow = nullptr;
        g_ImGuiEglContext = EGL_NO_CONTEXT;
        if (!InitImGuiOnGameContext(display, surface))
            return g_OrigEglSwapBuffers(display, surface);
    }

    // Query actual EGL surface dimensions.
    EGLint sw = 0, sh = 0;
    eglQuerySurface(display, surface, EGL_WIDTH, &sw);
    eglQuerySurface(display, surface, EGL_HEIGHT, &sh);

    // ImGui frame.
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplAndroid_NewFrame();

    // Use DisplayFramebufferScale to map logical coordinates to framebuffer pixels.
    {
        ImGuiIO &ioUpdate = ImGui::GetIO();
        // Keep DisplaySize in the ANativeWindow coordinate space used by input.
        float dispW = ioUpdate.DisplaySize.x;
        float dispH = ioUpdate.DisplaySize.y;
        g_Width  = (int)dispW;
        g_Height = (int)dispH;
        // Framebuffer scale is EGL surface extent divided by logical display extent.
        if (sw > 0 && sh > 0 && dispW > 0 && dispH > 0)
        {
            ioUpdate.DisplayFramebufferScale = ImVec2((float)sw / dispW, (float)sh / dispH);
        }
    }

    ImGui::NewFrame();
    ImGuiSoftKeyboard::PreUpdate();

    if (g_RenderCallback)
        g_RenderCallback();

    ImGuiSoftKeyboard::Draw();
    ImGui::Render();

    // Save host GL state.
    GLint prevFBO = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    GLint prevViewport[4];
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    GLboolean prevBlend = glIsEnabled(GL_BLEND);
    GLint prevBlendSrcRGB, prevBlendDstRGB, prevBlendSrcA, prevBlendDstA;
    glGetIntegerv(GL_BLEND_SRC_RGB, &prevBlendSrcRGB);
    glGetIntegerv(GL_BLEND_DST_RGB, &prevBlendDstRGB);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &prevBlendSrcA);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &prevBlendDstA);

    // Render into the host's default framebuffer, FBO 0.
    // Do not clear the host frame contents.
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Set viewport from the actual EGL surface extent.
    ImGuiIO &io = ImGui::GetIO();
    int fbW = (int)(io.DisplaySize.x * io.DisplayFramebufferScale.x);
    int fbH = (int)(io.DisplaySize.y * io.DisplayFramebufferScale.y);
    glViewport(0, 0, fbW, fbH);

    // Enable alpha blending for the overlay.
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    // Restore host GL state.
    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    if (prevBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    glBlendFuncSeparate(prevBlendSrcRGB, prevBlendDstRGB, prevBlendSrcA, prevBlendDstA);

    // Call the original eglSwapBuffers.
    return g_OrigEglSwapBuffers(display, surface);
}

/**
 * @brief eglSwapBuffersWithDamageKHR hook
 */
static EGLBoolean Hooked_eglSwapBuffersWithDamageKHR(
    EGLDisplay display, EGLSurface surface, EGLint* rects, EGLint n_rects)
{
    if (eglGetCurrentContext() == EGL_NO_CONTEXT)
        return g_OrigEglSwapBuffersWithDamage(display, surface, rects, n_rects);

    // Skip EGL rendering while Vulkan owns ImGui rendering.
    if (g_VkInitialized)
        return g_OrigEglSwapBuffersWithDamage(display, surface, rects, n_rects);

    if (!g_GlesInitialized)
    {
        if (!InitImGuiOnGameContext(display, surface))
            return g_OrigEglSwapBuffersWithDamage(display, surface, rects, n_rects);
    }

    ANativeWindow* currentWindow = AndroidPlatform::GetNativeWindow();
    if (!currentWindow)
        return g_OrigEglSwapBuffersWithDamage(display, surface, rects, n_rects);

    EGLContext currentCtx = eglGetCurrentContext();
    if (g_GlesInitialized && (currentWindow != g_ImGuiWindow || currentCtx != g_ImGuiEglContext))
    {
        SCH_LOGI("[SwapChainHook] WithDamage: EGL changed: window %p->%p  ctx %p->%p, reinitializing ImGui",
                 g_ImGuiWindow, currentWindow, g_ImGuiEglContext, currentCtx);
        g_ImGuiReady.store(false);
        ImGui_ImplOpenGL3_Shutdown();
        ImGuiSetup::TeardownContext();
        g_GlesInitialized = false;
        g_ImGuiWindow = nullptr;
        g_ImGuiEglContext = EGL_NO_CONTEXT;
        if (!InitImGuiOnGameContext(display, surface))
            return g_OrigEglSwapBuffersWithDamage(display, surface, rects, n_rects);
    }

    EGLint sw = 0, sh = 0;
    eglQuerySurface(display, surface, EGL_WIDTH, &sw);
    eglQuerySurface(display, surface, EGL_HEIGHT, &sh);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplAndroid_NewFrame();

    {
        ImGuiIO &ioUpdate = ImGui::GetIO();
        float dispW = ioUpdate.DisplaySize.x;
        float dispH = ioUpdate.DisplaySize.y;
        g_Width  = (int)dispW;
        g_Height = (int)dispH;
        if (sw > 0 && sh > 0 && dispW > 0 && dispH > 0)
            ioUpdate.DisplayFramebufferScale = ImVec2((float)sw / dispW, (float)sh / dispH);
    }

    ImGui::NewFrame();
    ImGuiSoftKeyboard::PreUpdate();

    if (g_RenderCallback)
        g_RenderCallback();

    ImGuiSoftKeyboard::Draw();
    ImGui::Render();

    GLint prevFBO = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    GLint prevViewport[4];
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    GLboolean prevBlend = glIsEnabled(GL_BLEND);
    GLint prevBlendSrcRGB, prevBlendDstRGB, prevBlendSrcA, prevBlendDstA;
    glGetIntegerv(GL_BLEND_SRC_RGB, &prevBlendSrcRGB);
    glGetIntegerv(GL_BLEND_DST_RGB, &prevBlendDstRGB);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &prevBlendSrcA);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &prevBlendDstA);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    ImGuiIO &io = ImGui::GetIO();
    int fbW = (int)(io.DisplaySize.x * io.DisplayFramebufferScale.x);
    int fbH = (int)(io.DisplaySize.y * io.DisplayFramebufferScale.y);
    glViewport(0, 0, fbW, fbH);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    if (prevBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    glBlendFuncSeparate(prevBlendSrcRGB, prevBlendDstRGB, prevBlendSrcA, prevBlendDstA);

    return g_OrigEglSwapBuffersWithDamage(display, surface, rects, n_rects);
}

// ---------------------------------------------------------------------------
// Vulkan path.
// ---------------------------------------------------------------------------

// Vulkan function pointers.
static PFN_vkQueuePresentKHR       g_OrigVkQueuePresent      = nullptr;
static PFN_vkCreateDevice          g_OrigVkCreateDevice      = nullptr;
static PFN_vkDestroyDevice         g_OrigVkDestroyDevice     = nullptr;
static PFN_vkGetDeviceQueue        g_OrigVkGetDeviceQueue    = nullptr;
static PFN_vkCreateSwapchainKHR    g_OrigVkCreateSwapchain   = nullptr;
static PFN_vkDestroySwapchainKHR   g_OrigVkDestroySwapchain  = nullptr;

// Record Vulkan Dobby hook addresses for restoration; EGL installations are not all registered.
static std::mutex                  g_HookedAddrsMutex;
static std::vector<void*>          g_DobbyHookedAddrs;

// Captured Vulkan objects.
static VkPhysicalDevice g_VkPhysDev     = VK_NULL_HANDLE;
static VkDevice         g_VkDevice      = VK_NULL_HANDLE;
static VkQueue          g_VkQueue       = VK_NULL_HANDLE;
static uint32_t         g_VkQueueFamily = UINT32_MAX;

// ImGui Vulkan rendering resources.
static VkRenderPass      g_VkRenderPass      = VK_NULL_HANDLE;
static VkCommandPool     g_VkCommandPool     = VK_NULL_HANDLE;
static VkDescriptorPool  g_VkDescriptorPool  = VK_NULL_HANDLE;
static VkSwapchainKHR    g_VkSwapchain       = VK_NULL_HANDLE;

static std::vector<VkImage>          g_VkSwapImages;
static std::vector<VkImageView>      g_VkSwapImageViews;
static std::vector<VkFramebuffer>    g_VkFramebuffers;
static std::vector<VkCommandBuffer>  g_VkCommandBuffers;
static std::vector<VkFence>          g_VkFences;

static VkFormat   g_VkSwapFormat = VK_FORMAT_UNDEFINED;
static VkExtent2D g_VkSwapExtent = {};  // Actual swapchain extent, which may be portrait.
static VkSurfaceTransformFlagBitsKHR g_VkPreTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;

/**
 * Whether preTransform includes a 90- or 270-degree rotation.
 */
static bool IsRotated90or270(VkSurfaceTransformFlagBitsKHR transform)
{
    return (transform & (VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR |
                         VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR)) != 0;
}

/**
 * Rotate vertices and clip rectangles in DisplaySize coordinates. For 90 degrees, (x, y) maps to
 * (dispH - y, x); for 270 degrees, it maps to (y, dispW - x). FramebufferScale then maps the
 * rotated logical extent to swapchain pixels.
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 */
static void RotateImDrawData(ImDrawData *drawData,
                             VkSurfaceTransformFlagBitsKHR transform,
                             VkExtent2D fbExtent)
{
    // Vertex coordinates use DisplaySize space.
    float dispW = drawData->DisplaySize.x;
    float dispH = drawData->DisplaySize.y;

    bool is90  = (transform & VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR)  != 0;
    bool is270 = (transform & VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR) != 0;

    if (!is90 && !is270)
        return;

    // Rotate vertices in logical display coordinates.
    for (int n = 0; n < drawData->CmdListsCount; n++)
    {
        ImDrawList *cmdList = drawData->CmdLists[n];
        for (int i = 0; i < cmdList->VtxBuffer.Size; i++)
        {
            ImDrawVert &v = cmdList->VtxBuffer[i];
            float x = v.pos.x;
            float y = v.pos.y;
            if (is90)
            {
                v.pos.x = dispH - y;
                v.pos.y = x;
            }
            else // is270
            {
                v.pos.x = y;
                v.pos.y = dispW - x;
            }
        }

        // Rotate clip rectangles.
        for (int i = 0; i < cmdList->CmdBuffer.Size; i++)
        {
            ImDrawCmd &cmd = cmdList->CmdBuffer[i];
            float cx1 = cmd.ClipRect.x;
            float cy1 = cmd.ClipRect.y;
            float cx2 = cmd.ClipRect.z;
            float cy2 = cmd.ClipRect.w;
            if (is90)
            {
                cmd.ClipRect = ImVec4(dispH - cy2, cx1, dispH - cy1, cx2);
            }
            else // is270
            {
                cmd.ClipRect = ImVec4(cy1, dispW - cx2, cy2, dispW - cx1);
            }
        }
    }

    // Rotation exchanges the logical width and height.
    // Map rotated logical coordinates to framebuffer pixels with FramebufferScale.
    float rotW = dispH;  // Rotated logical width.
    float rotH = dispW;  // Rotated logical height.
    float fbW  = (float)fbExtent.width;
    float fbH  = (float)fbExtent.height;

    drawData->DisplaySize      = ImVec2(rotW, rotH);
    drawData->DisplayPos       = ImVec2(0, 0);
    drawData->FramebufferScale = ImVec2(fbW / rotW, fbH / rotH);
}

/**
 * Release ImGui Vulkan resources.
 */
static void CleanupVkResources()
{
    if (g_VkDevice == VK_NULL_HANDLE)
        return;

    vkDeviceWaitIdle(g_VkDevice);

    if (g_VkInitialized)
        ImGui_ImplVulkan_Shutdown();

    for (auto f : g_VkFences)
        if (f) vkDestroyFence(g_VkDevice, f, nullptr);
    g_VkFences.clear();

    g_VkCommandBuffers.clear();

    for (auto fb : g_VkFramebuffers)
        if (fb) vkDestroyFramebuffer(g_VkDevice, fb, nullptr);
    g_VkFramebuffers.clear();

    for (auto iv : g_VkSwapImageViews)
        if (iv) vkDestroyImageView(g_VkDevice, iv, nullptr);
    g_VkSwapImageViews.clear();

    g_VkSwapImages.clear();

    if (g_VkRenderPass)    { vkDestroyRenderPass(g_VkDevice, g_VkRenderPass, nullptr);       g_VkRenderPass    = VK_NULL_HANDLE; }
    if (g_VkCommandPool)   { vkDestroyCommandPool(g_VkDevice, g_VkCommandPool, nullptr);     g_VkCommandPool   = VK_NULL_HANDLE; }
    if (g_VkDescriptorPool){ vkDestroyDescriptorPool(g_VkDevice, g_VkDescriptorPool, nullptr);g_VkDescriptorPool= VK_NULL_HANDLE; }

    g_VkInitialized = false;
}

/**
 * Create render-pass and framebuffer resources. LOAD_OP_LOAD preserves host image contents for
 * overlay drawing.
 *
 */
static bool CreateVkImGuiResources()
{
    if (g_VkDevice == VK_NULL_HANDLE || g_VkSwapImages.empty())
        return false;

    // Load the existing host frame for overlay rendering.
    VkAttachmentDescription attachment{};
    attachment.format         = g_VkSwapFormat;
    attachment.samples        = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD;     // Preserve host frame contents.
    attachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout  = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    attachment.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &colorRef;

    VkSubpassDependency dep{};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = 0;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpInfo{};
    rpInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpInfo.attachmentCount = 1;
    rpInfo.pAttachments    = &attachment;
    rpInfo.subpassCount    = 1;
    rpInfo.pSubpasses      = &subpass;
    rpInfo.dependencyCount = 1;
    rpInfo.pDependencies   = &dep;

    if (vkCreateRenderPass(g_VkDevice, &rpInfo, nullptr, &g_VkRenderPass) != VK_SUCCESS)
    {
        LOGE("[SwapChainHook/Vk] vkCreateRenderPass failed");
        return false;
    }

    // -- ImageView + Framebuffer --
    uint32_t imageCount = (uint32_t)g_VkSwapImages.size();
    g_VkSwapImageViews.resize(imageCount);
    g_VkFramebuffers.resize(imageCount);

    for (uint32_t i = 0; i < imageCount; i++)
    {
        VkImageViewCreateInfo ivInfo{};
        ivInfo.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ivInfo.image    = g_VkSwapImages[i];
        ivInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ivInfo.format   = g_VkSwapFormat;
        ivInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        ivInfo.subresourceRange.baseMipLevel   = 0;
        ivInfo.subresourceRange.levelCount     = 1;
        ivInfo.subresourceRange.baseArrayLayer = 0;
        ivInfo.subresourceRange.layerCount     = 1;

        if (vkCreateImageView(g_VkDevice, &ivInfo, nullptr, &g_VkSwapImageViews[i]) != VK_SUCCESS)
        {
            LOGE("[SwapChainHook/Vk] vkCreateImageView[%u] failed", i);
            return false;
        }

        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass      = g_VkRenderPass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments    = &g_VkSwapImageViews[i];
        fbInfo.width           = g_VkSwapExtent.width;
        fbInfo.height          = g_VkSwapExtent.height;
        fbInfo.layers          = 1;

        if (vkCreateFramebuffer(g_VkDevice, &fbInfo, nullptr, &g_VkFramebuffers[i]) != VK_SUCCESS)
        {
            LOGE("[SwapChainHook/Vk] vkCreateFramebuffer[%u] failed", i);
            return false;
        }
    }

    // -- CommandPool + CommandBuffers --
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = g_VkQueueFamily;

    if (vkCreateCommandPool(g_VkDevice, &poolInfo, nullptr, &g_VkCommandPool) != VK_SUCCESS)
    {
        LOGE("[SwapChainHook/Vk] vkCreateCommandPool failed");
        return false;
    }

    g_VkCommandBuffers.resize(imageCount);
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool        = g_VkCommandPool;
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = imageCount;

    if (vkAllocateCommandBuffers(g_VkDevice, &allocInfo, g_VkCommandBuffers.data()) != VK_SUCCESS)
    {
        LOGE("[SwapChainHook/Vk] vkAllocateCommandBuffers failed");
        return false;
    }

    // One fence per image tracks completion of its command buffer.
    g_VkFences.resize(imageCount);
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t i = 0; i < imageCount; i++)
    {
        if (vkCreateFence(g_VkDevice, &fenceInfo, nullptr, &g_VkFences[i]) != VK_SUCCESS)
        {
            LOGE("[SwapChainHook/Vk] vkCreateFence[%u] failed", i);
            return false;
        }
    }

    // Descriptor pool for ImGui textures.
    VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16 },
    };
    VkDescriptorPoolCreateInfo dpInfo{};
    dpInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpInfo.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    dpInfo.maxSets       = 16;
    dpInfo.poolSizeCount = 1;
    dpInfo.pPoolSizes    = poolSizes;

    if (vkCreateDescriptorPool(g_VkDevice, &dpInfo, nullptr, &g_VkDescriptorPool) != VK_SUCCESS)
    {
        LOGE("[SwapChainHook/Vk] vkCreateDescriptorPool failed");
        return false;
    }

    SCH_LOGI("[SwapChainHook/Vk] Resources created  images=%u  %ux%u",
         imageCount, g_VkSwapExtent.width, g_VkSwapExtent.height);
    return true;
}

/**
 * Initialize the ImGui Vulkan renderer.
 */
static bool InitImGuiVulkan()
{
    // Select logical dimensions normalized to landscape.
    // Exchange dimensions for 90- or 270-degree preTransform.
    uint32_t displayW = g_VkSwapExtent.width;
    uint32_t displayH = g_VkSwapExtent.height;
    if (IsRotated90or270(g_VkPreTransform))
    {
        std::swap(displayW, displayH);
        SCH_LOGI("[SwapChainHook/Vk] preTransform=0x%x, swapped display %ux%u → %ux%u",
             (int)g_VkPreTransform, g_VkSwapExtent.width, g_VkSwapExtent.height, displayW, displayH);
    }

    ANativeWindow* vkWindow = AndroidPlatform::GetNativeWindow();
    if (!vkWindow)
    {
        LOGE("[SwapChainHook/Vk] ANativeWindow is null, cannot init ImGui");
        return false;
    }

    ImGuiSetup::EnsureContext(vkWindow);
    ImGui::GetIO().DisplaySize = ImVec2((float)displayW, (float)displayH);

    // Vulkan renderer.
    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.Instance        = VK_NULL_HANDLE; // This configuration does not request additional dynamic-rendering resources.
    initInfo.PhysicalDevice  = g_VkPhysDev;
    initInfo.Device          = g_VkDevice;
    initInfo.QueueFamily     = g_VkQueueFamily;
    initInfo.Queue           = g_VkQueue;
    initInfo.DescriptorPool  = g_VkDescriptorPool;
    initInfo.RenderPass      = g_VkRenderPass;
    initInfo.MinImageCount   = (uint32_t)g_VkSwapImages.size();
    initInfo.ImageCount      = (uint32_t)g_VkSwapImages.size();
    initInfo.MSAASamples     = VK_SAMPLE_COUNT_1_BIT;
    initInfo.CheckVkResultFn = [](VkResult r) {
        if (r != VK_SUCCESS) LOGE("[SwapChainHook/Vk] ImGui VkResult=%d", (int)r);
    };

    if (!ImGui_ImplVulkan_Init(&initInfo))
    {
        LOGE("[SwapChainHook/Vk] ImGui_ImplVulkan_Init failed");
        return false;
    }

    // Fonts.
    ResourceManager::GetInstance().initializeFonts(30.0f);

    // Report landscape dimensions.
    g_Width = (int)displayW;
    g_Height = (int)displayH;
    g_VkInitialized = true;
    g_ImGuiReady.store(true);

    SCH_LOGI("[SwapChainHook/Vk] ImGui initialized  display=%ux%u  fb=%ux%u  preTransform=0x%x",
         displayW, displayH, g_VkSwapExtent.width, g_VkSwapExtent.height, (int)g_VkPreTransform);
    return true;
}

// ---------------------------------------------------------------------------
// Vulkan Hooks
// ---------------------------------------------------------------------------

/**
 * vkCreateDevice hook captures the physical/logical device and queue family.
 */
static VkResult VKAPI_CALL Hooked_vkCreateDevice(
    VkPhysicalDevice physicalDevice,
    const VkDeviceCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkDevice *pDevice)
{
    VkResult result = g_OrigVkCreateDevice(physicalDevice, pCreateInfo, pAllocator, pDevice);
    if (result == VK_SUCCESS && pDevice)
    {
        g_VkPhysDev = physicalDevice;
        g_VkDevice  = *pDevice;

        // Select the graphics queue family from DeviceCreateInfo.
        if (pCreateInfo && pCreateInfo->queueCreateInfoCount > 0)
        {
            g_VkQueueFamily = pCreateInfo->pQueueCreateInfos[0].queueFamilyIndex;
        }

        SCH_LOGI("[SwapChainHook/Vk] Captured device=%p  physDev=%p  queueFamily=%u",
             g_VkDevice, g_VkPhysDev, g_VkQueueFamily);
    }
    return result;
}

/**
 * vkGetDeviceQueue hook captures the graphics queue.
 */
static void VKAPI_CALL Hooked_vkGetDeviceQueue(
    VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex, VkQueue *pQueue)
{
    g_OrigVkGetDeviceQueue(device, queueFamilyIndex, queueIndex, pQueue);
    if (pQueue && *pQueue && queueFamilyIndex == g_VkQueueFamily)
    {
        g_VkQueue = *pQueue;
        SCH_LOGI("[SwapChainHook/Vk] Captured queue=%p  family=%u", g_VkQueue, queueFamilyIndex);
    }
}

/**
 * vkCreateSwapchainKHR hook captures swapchain information and creates rendering resources.
 */
static VkResult VKAPI_CALL Hooked_vkCreateSwapchainKHR(
    VkDevice device,
    const VkSwapchainCreateInfoKHR *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkSwapchainKHR *pSwapchain)
{
    // Release resources before swapchain recreation or an EGL-to-Vulkan transition.
    if (g_VkInitialized)
    {
        CleanupVkResources();
        ImGuiSetup::TeardownContext();
    }
    else if (g_GlesInitialized)
    {
        // Release EGL ImGui resources before activating Vulkan.
        SCH_LOGI("[SwapChainHook/Vk] EGL→Vulkan transition, cleaning up EGL ImGui");
        g_ImGuiReady.store(false);
        ImGui_ImplOpenGL3_Shutdown();
        ImGuiSetup::TeardownContext();
        g_GlesInitialized = false;
        g_ImGuiWindow = nullptr;
        g_ImGuiEglContext = EGL_NO_CONTEXT;
    }

    VkResult result = g_OrigVkCreateSwapchain(device, pCreateInfo, pAllocator, pSwapchain);
    if (result != VK_SUCCESS || !pSwapchain)
        return result;

    g_VkSwapchain    = *pSwapchain;
    g_VkSwapFormat   = pCreateInfo->imageFormat;
    g_VkSwapExtent   = pCreateInfo->imageExtent;
    g_VkPreTransform = (VkSurfaceTransformFlagBitsKHR)pCreateInfo->preTransform;

    // Get swapchain images.
    uint32_t imageCount = 0;
    vkGetSwapchainImagesKHR(device, *pSwapchain, &imageCount, nullptr);
    g_VkSwapImages.resize(imageCount);
    vkGetSwapchainImagesKHR(device, *pSwapchain, &imageCount, g_VkSwapImages.data());

    SCH_LOGI("[SwapChainHook/Vk] Swapchain created  %ux%u  images=%u  format=%d",
         g_VkSwapExtent.width, g_VkSwapExtent.height, imageCount, (int)g_VkSwapFormat);

    // Create ImGui rendering resources.
    if (g_VkDevice != VK_NULL_HANDLE && g_VkQueue != VK_NULL_HANDLE)
    {
        if (CreateVkImGuiResources())
            InitImGuiVulkan();
    }

    return result;
}

/**
 * vkDestroySwapchainKHR hook releases rendering resources.
 */
static void VKAPI_CALL Hooked_vkDestroySwapchainKHR(
    VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks *pAllocator)
{
    if (swapchain == g_VkSwapchain && g_VkInitialized)
    {
        CleanupVkResources();
        ImGuiSetup::TeardownContext();
        g_ImGuiReady.store(false);
    }
    g_OrigVkDestroySwapchain(device, swapchain, pAllocator);
}

/**
 * vkDestroyDevice hook releases device-associated state.
 */
static void VKAPI_CALL Hooked_vkDestroyDevice(
    VkDevice device, const VkAllocationCallbacks *pAllocator)
{
    if (device == g_VkDevice)
    {
        CleanupVkResources();
        if (ImGui::GetCurrentContext())
        {
            ImGuiSetup::TeardownContext();
        }
        g_ImGuiReady.store(false);
        g_VkDevice = VK_NULL_HANDLE;
        g_VkQueue  = VK_NULL_HANDLE;
    }
    g_OrigVkDestroyDevice(device, pAllocator);
}

/**
 * vkQueuePresentKHR hook records and submits ImGui after waiting on the host's present
 * semaphores. The current path lacks a complete overlay-to-present signal chain and mutates
 * caller-owned present data.
 *
 *
 *
 *
 */
static VkResult VKAPI_CALL Hooked_vkQueuePresentKHR(
    VkQueue queue, const VkPresentInfoKHR *pPresentInfo)
{
    if (!pPresentInfo || pPresentInfo->swapchainCount == 0)
        return g_OrigVkQueuePresent(queue, pPresentInfo);

    if (!g_VkInitialized)
        return g_OrigVkQueuePresent(queue, pPresentInfo);

    // Find the tracked swapchain.
    for (uint32_t sc = 0; sc < pPresentInfo->swapchainCount; sc++)
    {
        if (pPresentInfo->pSwapchains[sc] != g_VkSwapchain)
            continue;

        uint32_t imageIndex = pPresentInfo->pImageIndices[sc];
        if (imageIndex >= g_VkCommandBuffers.size())
            continue;

        // Wait for the command buffer's previous GPU work.
        vkWaitForFences(g_VkDevice, 1, &g_VkFences[imageIndex], VK_TRUE, UINT64_MAX);
        vkResetFences(g_VkDevice, 1, &g_VkFences[imageIndex]);

        // Begin an ImGui frame.
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplAndroid_NewFrame();

        {
            ImGuiIO &io = ImGui::GetIO();
            g_Width  = (int)io.DisplaySize.x;
            g_Height = (int)io.DisplaySize.y;
        }

        ImGui::NewFrame();
        ImGuiSoftKeyboard::PreUpdate();

        if (g_RenderCallback)
            g_RenderCallback();

        ImGuiSoftKeyboard::Draw();

        ImGui::Render();

        // Rotate draw data to the framebuffer orientation.
        if (IsRotated90or270(g_VkPreTransform))
            RotateImDrawData(ImGui::GetDrawData(), g_VkPreTransform, g_VkSwapExtent);

        // Record commands.
        VkCommandBuffer cmd = g_VkCommandBuffers[imageIndex];
        vkResetCommandBuffer(cmd, 0);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &beginInfo);

        VkRenderPassBeginInfo rpBegin{};
        rpBegin.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpBegin.renderPass        = g_VkRenderPass;
        rpBegin.framebuffer       = g_VkFramebuffers[imageIndex];
        rpBegin.renderArea.extent = g_VkSwapExtent;

        vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
        vkCmdEndRenderPass(cmd);
        vkEndCommandBuffer(cmd);

        // Submit after waiting on host present semaphores.
        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

        VkSubmitInfo submitInfo{};
        submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers    = &cmd;

        // Use the host wait semaphores for the overlay submission.
        // These waits order overlay rendering after host rendering.
        if (pPresentInfo->waitSemaphoreCount > 0 && pPresentInfo->pWaitSemaphores)
        {
            submitInfo.waitSemaphoreCount = pPresentInfo->waitSemaphoreCount;
            submitInfo.pWaitSemaphores    = pPresentInfo->pWaitSemaphores;
            // Supply one destination stage for each wait semaphore.
            std::vector<VkPipelineStageFlags> waitStages(pPresentInfo->waitSemaphoreCount, waitStage);
            submitInfo.pWaitDstStageMask  = waitStages.data();

            vkQueueSubmit(queue, 1, &submitInfo, g_VkFences[imageIndex]);

            // Current limitation: host waits are cleared without an overlay-to-present replacement signal.
            // Current limitation: this const_cast mutates caller-owned PresentInfo.
            auto *mutablePresent = const_cast<VkPresentInfoKHR*>(pPresentInfo);
            mutablePresent->waitSemaphoreCount = 0;
            mutablePresent->pWaitSemaphores    = nullptr;
        }
        else
        {
            vkQueueSubmit(queue, 1, &submitInfo, g_VkFences[imageIndex]);
        }

        break; // Handle only the first matching swapchain.
    }

    return g_OrigVkQueuePresent(queue, pPresentInfo);
}

// ===========================================================================
// Public interface.
// ===========================================================================

namespace SwapChainHook
{

void Install()
{
    if (g_Installed.exchange(true))
        return;

    SCH_LOGI("[SwapChainHook] Installing hooks...");

    // Create a helper instance and device.
    VkInstance helperInst = VK_NULL_HANDLE;
    VkDevice helperDev = VK_NULL_HANDLE;
    VkPhysicalDevice helperPhysDev = VK_NULL_HANDLE;

    do {
        VkInstanceCreateInfo instCI{};
        instCI.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        VkResult r = vkCreateInstance(&instCI, nullptr, &helperInst);
        SCH_LOGI("[SwapChainHook] helper vkCreateInstance=%d  inst=%p", (int)r, helperInst);
        if (r != VK_SUCCESS || !helperInst) break;

        uint32_t pdCount = 0;
        vkEnumeratePhysicalDevices(helperInst, &pdCount, nullptr);
        if (pdCount == 0) { LOGE("[SwapChainHook] No physical devices found"); break; }

        std::vector<VkPhysicalDevice> pds(pdCount);
        vkEnumeratePhysicalDevices(helperInst, &pdCount, pds.data());
        helperPhysDev = pds[0];
        SCH_LOGI("[SwapChainHook] helper physDev=%p (%u total)", helperPhysDev, pdCount);

        // Find a graphics queue family.
        uint32_t qfCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(helperPhysDev, &qfCount, nullptr);
        std::vector<VkQueueFamilyProperties> qfProps(qfCount);
        vkGetPhysicalDeviceQueueFamilyProperties(helperPhysDev, &qfCount, qfProps.data());

        uint32_t gfxFamily = 0;
        for (uint32_t i = 0; i < qfCount; i++)
        {
            if (qfProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
                gfxFamily = i;
                break;
            }
        }

        float prio = 1.0f;
        VkDeviceQueueCreateInfo qCI{};
        qCI.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qCI.queueFamilyIndex = gfxFamily;
        qCI.queueCount = 1;
        qCI.pQueuePriorities = &prio;

        // Enable VK_KHR_swapchain to resolve vkQueuePresentKHR.
        const char* exts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
        VkDeviceCreateInfo devCI{};
        devCI.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        devCI.queueCreateInfoCount = 1;
        devCI.pQueueCreateInfos = &qCI;
        devCI.enabledExtensionCount = 1;
        devCI.ppEnabledExtensionNames = exts;

        r = vkCreateDevice(helperPhysDev, &devCI, nullptr, &helperDev);
        SCH_LOGI("[SwapChainHook] helper vkCreateDevice=%d  dev=%p", (int)r, helperDev);
    } while (false);

    // Resolve dispatch targets through vkGetDeviceProcAddr and install hooks.
    if (helperDev)
    {
        struct VkHookEntry { const char* name; void* hookFunc; void** origSlot; };
        VkHookEntry devHooks[] = {
            { "vkQueuePresentKHR",     (void*)Hooked_vkQueuePresentKHR,     (void**)&g_OrigVkQueuePresent      },
            { "vkCreateSwapchainKHR",  (void*)Hooked_vkCreateSwapchainKHR,  (void**)&g_OrigVkCreateSwapchain   },
            { "vkDestroySwapchainKHR", (void*)Hooked_vkDestroySwapchainKHR, (void**)&g_OrigVkDestroySwapchain  },
            { "vkDestroyDevice",       (void*)Hooked_vkDestroyDevice,       (void**)&g_OrigVkDestroyDevice     },
            { "vkGetDeviceQueue",      (void*)Hooked_vkGetDeviceQueue,      (void**)&g_OrigVkGetDeviceQueue    },
        };

        for (auto& h : devHooks)
        {
            void* target = (void*)vkGetDeviceProcAddr(helperDev, h.name);
            if (target)
            {
                int ret = DobbyHook(target, h.hookFunc, h.origSlot);
                SCH_LOGI("[SwapChainHook] DobbyHook(%s)=%d  target=%p  orig=%p",
                     h.name, ret, target, *(h.origSlot));
                if (ret == 0)
                {
                    std::lock_guard<std::mutex> lk(g_HookedAddrsMutex);
                    g_DobbyHookedAddrs.push_back(target);
                }
            }
            else
            {
                LOGE("[SwapChainHook] %s: vkGetDeviceProcAddr returned null", h.name);
            }
        }

        // Resolve instance-level vkCreateDevice with vkGetInstanceProcAddr.
        {
            void* target = (void*)vkGetInstanceProcAddr(helperInst, "vkCreateDevice");
            if (target)
            {
                int ret = DobbyHook(target, (void*)Hooked_vkCreateDevice, (void**)&g_OrigVkCreateDevice);
                SCH_LOGI("[SwapChainHook] DobbyHook(vkCreateDevice)=%d  target=%p  orig=%p",
                     ret, target, g_OrigVkCreateDevice);
                if (ret == 0)
                {
                    std::lock_guard<std::mutex> lk(g_HookedAddrsMutex);
                    g_DobbyHookedAddrs.push_back(target);
                }
            }
        }
    }
    else
    {
        LOGE("[SwapChainHook] Cannot create helper VkDevice, Vulkan hooks not installed");
    }

    // Destroy the helper device.
    // Dobby patches executable instructions; the patch is independent of helper-device lifetime.
    // The destruction hook passes through when device differs from g_VkDevice.
    if (helperDev)
    {
        vkDestroyDevice(helperDev, nullptr);
        SCH_LOGI("[SwapChainHook] helper device destroyed");
    }
    if (helperInst)
    {
        vkDestroyInstance(helperInst, nullptr);
        SCH_LOGI("[SwapChainHook] helper instance destroyed");
    }

    // === EGL hooks ===
    {
        void* libEGL = dlopen("libEGL.so", RTLD_NOW | RTLD_NOLOAD);
        if (libEGL)
        {
            void* sym = dlsym(libEGL, "eglSwapBuffers");
            if (sym)
            {
                DobbyHook(sym, (void*)Hooked_eglSwapBuffers, (void**)&g_OrigEglSwapBuffers);
                SCH_LOGI("[SwapChainHook] eglSwapBuffers hooked at %p", sym);
            }
            void* symDmg = dlsym(libEGL, "eglSwapBuffersWithDamageKHR");
            if (!symDmg) symDmg = (void*)eglGetProcAddress("eglSwapBuffersWithDamageKHR");
            if (symDmg)
            {
                DobbyHook(symDmg, (void*)Hooked_eglSwapBuffersWithDamageKHR, (void**)&g_OrigEglSwapBuffersWithDamage);
                SCH_LOGI("[SwapChainHook] eglSwapBuffersWithDamageKHR hooked at %p", symDmg);
            }
            dlclose(libEGL);
        }
    }

    SCH_LOGI("[SwapChainHook] Hooks installed");
}

void Uninstall()
{
    if (!g_Installed.exchange(false))
        return;

    SCH_LOGI("[SwapChainHook] Uninstalling...");

    // Unhook all DobbyHooked addresses
    {
        std::lock_guard<std::mutex> lk(g_HookedAddrsMutex);
        for (void* addr : g_DobbyHookedAddrs)
            DobbyDestroy(addr);
        g_DobbyHookedAddrs.clear();
    }

    g_OrigEglSwapBuffers = nullptr;
    g_OrigEglSwapBuffersWithDamage = nullptr;

    // Clean up ImGui.
    if (g_VkInitialized)
        CleanupVkResources();

    if (ImGui::GetCurrentContext())
    {
        if (g_GlesInitialized)
            ImGui_ImplOpenGL3_Shutdown();
        ImGuiSetup::TeardownContext();
    }

    g_GlesInitialized = false;
    g_VkInitialized   = false;
    g_ImGuiReady.store(false);

    SCH_LOGI("[SwapChainHook] Uninstalled");
}

bool IsInitialized()
{
    return g_ImGuiReady.load();
}

void SetRenderCallback(std::function<void()> callback)
{
    std::lock_guard<std::mutex> lk(g_CallbackMutex);
    g_RenderCallback = std::move(callback);
}

int GetWidth()  { return g_Width; }
int GetHeight() { return g_Height; }

} // namespace SwapChainHook
