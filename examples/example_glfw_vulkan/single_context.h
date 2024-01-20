//
// Created by Michel Lesoinne on 1/21/24.
//

#pragma once
#include <cstdio>

#ifdef MULTI_THREADED
#else
template <typename F>
void wait_for_main(F&& functor)
{
    functor();
}
#endif

struct ThreadWindow
{
    static int nActive;

    ThreadWindow()
    {
        ++nActive;
    }


    void FramePresent(ImGui_ImplVulkanH_Window* wd)
    {
        if (swapChainRebuild)
            return;
        VkSemaphore render_complete_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;
        VkPresentInfoKHR info = {};
        info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        info.waitSemaphoreCount = 1;
        info.pWaitSemaphores = &render_complete_semaphore;
        info.swapchainCount = 1;
        info.pSwapchains = &wd->Swapchain;
        info.pImageIndices = &wd->FrameIndex;
        VkResult err = vkQueuePresentKHR(g_Queue, &info);
        if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR)
        {
            swapChainRebuild = true;
            return;
        }
        check_vk_result(err);
        wd->SemaphoreIndex = (wd->SemaphoreIndex + 1) % wd->ImageCount; // Now we can use the next set of semaphores
    }

    void FrameRender(ImGuiContext* ctx, ImGui_ImplVulkanH_Window* wd, ImDrawData* draw_data)
    {
        VkResult err;

        VkSemaphore image_acquired_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].ImageAcquiredSemaphore;
        VkSemaphore render_complete_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;
        err = vkAcquireNextImageKHR(g_Device, wd->Swapchain, UINT64_MAX, image_acquired_semaphore, VK_NULL_HANDLE,
                                    &wd->FrameIndex);
        if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR)
        {
            swapChainRebuild = true;
            return;
        }
        check_vk_result(err);

        ImGui_ImplVulkanH_Frame* fd = &wd->Frames[wd->FrameIndex];
        {
            err = vkWaitForFences(g_Device, 1, &fd->Fence, VK_TRUE, UINT64_MAX);
            // wait indefinitely instead of periodically checking
            check_vk_result(err);

            err = vkResetFences(g_Device, 1, &fd->Fence);
            check_vk_result(err);
        }
        {
            err = vkResetCommandPool(g_Device, fd->CommandPool, 0);
            check_vk_result(err);
            VkCommandBufferBeginInfo info = {};
            info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            err = vkBeginCommandBuffer(fd->CommandBuffer, &info);
            check_vk_result(err);
        }
        {
            VkRenderPassBeginInfo info = {};
            info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            info.renderPass = wd->RenderPass;
            info.framebuffer = fd->Framebuffer;
            info.renderArea.extent.width = wd->Width;
            info.renderArea.extent.height = wd->Height;
            info.clearValueCount = 1;
            info.pClearValues = &wd->ClearValue;
            vkCmdBeginRenderPass(fd->CommandBuffer, &info, VK_SUBPASS_CONTENTS_INLINE);
        }

        // Record dear imgui primitives into command buffer
        ImGui_ImplVulkan_RenderDrawData(ctx, draw_data, fd->CommandBuffer);

        // Submit command buffer
        vkCmdEndRenderPass(fd->CommandBuffer);
        {
            VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            VkSubmitInfo info = {};
            info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            info.waitSemaphoreCount = 1;
            info.pWaitSemaphores = &image_acquired_semaphore;
            info.pWaitDstStageMask = &wait_stage;
            info.commandBufferCount = 1;
            info.pCommandBuffers = &fd->CommandBuffer;
            info.signalSemaphoreCount = 1;
            info.pSignalSemaphores = &render_complete_semaphore;

            err = vkEndCommandBuffer(fd->CommandBuffer);
            check_vk_result(err);
            err = vkQueueSubmit(g_Queue, 1, &info, fd->Fence);
            check_vk_result(err);
        }
    }

    /// Must be run on main thread.
    void setupWindow()
    {
        window = glfwCreateWindow(1280, 720, "Dear ImGui GLFW+Vulkan example", nullptr, nullptr);
        // Create Window Surface
        VkResult err = glfwCreateWindowSurface(g_Instance, window, g_Allocator, &surface);
        check_vk_result(err);

        // Create Framebuffers
        glfwGetFramebufferSize(window, &w, &h);
    }

    static VkDescriptorPool makeDescriptorPool()
    {
        VkDescriptorPoolSize pool_sizes[] =
        {
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 128},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 128}
        };
        VkDescriptorPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool_info.maxSets = 1;
        pool_info.poolSizeCount = (uint32_t)IM_ARRAYSIZE(pool_sizes);
        pool_info.pPoolSizes = pool_sizes;
        VkDescriptorPool descriptor_pool;
        auto err = vkCreateDescriptorPool(g_Device, &pool_info, g_Allocator, &descriptor_pool);
        check_vk_result(err);
        return descriptor_pool;
    }

    void CleanupVulkanWindow()
    {
        ImGui_ImplVulkanH_DestroyWindow(g_Instance, g_Device, &myWindowData, g_Allocator);
    }

    void fullSetup()
    {
        wait_for_main([this] { setupWindow(); });
        ctx = ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(ctx);
        (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad; // Enable Gamepad Controls
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable; // Enable Docking
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable; // Enable Multi-Viewport / Platform Windows

        ImGui_ImplVulkanH_Window* wd = &myWindowData;
        SetupVulkanWindow(ctx, wd, surface, w, h);
        // Setup Dear ImGui style
        ImGui::StyleColorsDark(ctx);
        // ImGui::StyleColorsLight(ctx);

        // When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
        ImGuiStyle& style = ImGui::GetStyle(ctx);
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            style.WindowRounding = 0.0f;
            style.Colors[ImGuiCol_WindowBg].w = 1.0f;
        }

        // Setup Platform/Renderer backends
        ImGui_ImplGlfw_InitForVulkan(ctx, window, true);
        ImGui_ImplVulkan_InitInfo init_info = {};
        init_info.Instance = g_Instance;
        init_info.PhysicalDevice = g_PhysicalDevice;
        init_info.Device = g_Device;
        init_info.QueueFamily = g_QueueFamily;
        init_info.Queue = g_Queue;
        init_info.PipelineCache = g_PipelineCache;
        init_info.DescriptorPool = makeDescriptorPool();
        init_info.Subpass = 0;
        init_info.MinImageCount = g_MinImageCount;
        init_info.ImageCount = wd->ImageCount;
        init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        init_info.Allocator = g_Allocator;
        init_info.CheckVkResultFn = check_vk_result;
        ImGui_ImplVulkan_Init(ctx, &init_info, wd->RenderPass);
    }

    bool oneFrame()
    {
        if (glfwWindowShouldClose(window)) return false;

        auto wd = &myWindowData;
        ImGuiIO& io = ImGui::GetIO(ctx);
        // Poll and handle events (inputs, window resize, etc.)
        // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
        // - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
        // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
        // Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
        // glfwPollEvents();

        auto onMainThread = [&]()
        {
            // Resize swap chain?
            if (swapChainRebuild)
            {
                int width, height;
                glfwGetFramebufferSize(window, &width, &height);
                if (width > 0 && height > 0)
                {
                    ImGui_ImplVulkan_SetMinImageCount(ctx, g_MinImageCount);
                    ImGui_ImplVulkanH_CreateOrResizeWindow(ctx, g_Instance, g_PhysicalDevice, g_Device,
                                                           &myWindowData, g_QueueFamily, g_Allocator, width,
                                                           height, g_MinImageCount);
                    myWindowData.FrameIndex = 0;
                    swapChainRebuild = false;
                }
            }

            // Start the Dear ImGui frame
            ImGui_ImplVulkan_NewFrame(ctx);
            ImGui_ImplGlfw_NewFrame(ctx);
            ImGui::NewFrame(ctx);
        };
        wait_for_main(onMainThread);
        // 1. Show the big demo window (Most of the sample code is in ImGui::ShowDemoWindow()! You can browse its code to learn more about Dear ImGui!).
        if (show_demo_window)
            ImGui::ShowDemoWindow(ctx, &show_demo_window);

        // 2. Show a simple window that we create ourselves. We use a Begin/End pair to create a named window.
        {
            static float f = 0.0f;
            static int counter = 0;

            ImGui::Begin(ctx, "Hello, world!"); // Create a window called "Hello, world!" and append into it.

            ImGui::Text(ctx, "This is some useful text."); // Display some text (you can use a format strings too)
            ImGui::Checkbox(ctx, "Demo Window", &show_demo_window);
            // Edit bools storing our window open/close state
            ImGui::Checkbox(ctx, "Another Window", &show_another_window);

            ImGui::SliderFloat(ctx, "float", &f, 0.0f, 1.0f); // Edit 1 float using a slider from 0.0f to 1.0f
            ImGui::ColorEdit3(ctx, "clear color", (float*)&clear_color); // Edit 3 floats representing a color

            if (ImGui::Button(ctx, "Button"))
                // Buttons return true when clicked (most widgets return true when edited/activated)
                counter++;
            ImGui::SameLine(ctx);
            ImGui::Text(ctx, "counter = %d", counter);

            ImGui::Text(ctx, "Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
            ImGui::End(ctx);
        }

        // 3. Show another simple window.
        if (show_another_window)
        {
            ImGui::Begin(ctx, "Another Window", &show_another_window);
            // Pass a pointer to our bool variable (the window will have a closing button that will clear the bool when clicked)
            ImGui::Text(ctx, "Hello from another window!");
            if (ImGui::Button(ctx, "Close Me"))
                show_another_window = false;
            ImGui::End(ctx);
        }

        // Rendering
        ImGui::Render(ctx);
        ImDrawData* main_draw_data = ImGui::GetDrawData(ctx);
        const bool main_is_minimized = (main_draw_data->DisplaySize.x <= 0.0f || main_draw_data->DisplaySize.y <=
            0.0f);

        wd->ClearValue.color.float32[0] = clear_color.x * clear_color.w;
        wd->ClearValue.color.float32[1] = clear_color.y * clear_color.w;
        wd->ClearValue.color.float32[2] = clear_color.z * clear_color.w;
        wd->ClearValue.color.float32[3] = clear_color.w;
        if (!main_is_minimized)
            FrameRender(ctx, wd, main_draw_data);

        // Update and Render additional Platform Windows
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
            wait_for_main([this]
            {
                ImGui::UpdatePlatformWindows(ctx);
            });
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
            ImGui::RenderPlatformWindowsDefault(ctx);

        // Present Main Platform Window
        if (!main_is_minimized)
            FramePresent(wd);
        return true; // The window is still alive.
    }

    void close()
    {
        --nActive;
        isActive = false;
        wait_for_main([this]
        {
            ImGui_ImplVulkan_Shutdown(ctx);
            ImGui_ImplGlfw_Shutdown(ctx);
            ImGui::DestroyContext(ctx);
            glfwDestroyWindow(window);
            CleanupVulkanWindow();
        });
    }

    static VkAllocationCallbacks* g_Allocator;
    static VkInstance g_Instance;
    static VkPhysicalDevice g_PhysicalDevice;
    static VkDevice g_Device;
    static VkQueue g_Queue;
    static uint32_t g_QueueFamily;

    static VkPipelineCache g_PipelineCache;
    static VkDescriptorPool g_DescriptorPool;

    static int g_MinImageCount;

    bool isActive = true;
    GLFWwindow* window{};
    int w{}, h{};
    bool swapChainRebuild = false;
    ImGuiContext* ctx = nullptr;
    VkSurfaceKHR surface{};
    ImGui_ImplVulkanH_Window myWindowData{};
    bool show_demo_window = true;
    bool show_another_window = false;
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    static void check_vk_result(VkResult err)
    {
        if (err == 0)
            return;
        fprintf(stderr, "[vulkan] Error: VkResult = %d\n", err);
        if (err < 0)
            abort();
    }

    static bool IsExtensionAvailable(const ImVector<VkExtensionProperties>& properties, const char* extension)
    {
        for (const VkExtensionProperties& p : properties)
            if (strcmp(p.extensionName, extension) == 0)
                return true;
        return false;
    }


    static VkPhysicalDevice SetupVulkan_SelectPhysicalDevice()
    {
        uint32_t gpu_count;
        VkResult err = vkEnumeratePhysicalDevices(g_Instance, &gpu_count, nullptr);
        check_vk_result(err);
        IM_ASSERT(gpu_count > 0);

        ImVector<VkPhysicalDevice> gpus;
        gpus.resize(gpu_count);
        err = vkEnumeratePhysicalDevices(g_Instance, &gpu_count, gpus.Data);
        check_vk_result(err);

        // If a number >1 of GPUs got reported, find discrete GPU if present, or use first one available. This covers
        // most common cases (multi-gpu/integrated+dedicated graphics). Handling more complicated setups (multiple
        // dedicated GPUs) is out of scope of this sample.
        for (VkPhysicalDevice& device : gpus)
        {
            VkPhysicalDeviceProperties properties;
            vkGetPhysicalDeviceProperties(device, &properties);
            if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
                return device;
        }

        // Use first GPU (Integrated) is a Discrete one is not available.
        if (gpu_count > 0)
            return gpus[0];
        return VK_NULL_HANDLE;
    }

    static void SetupVulkan(ImVector<const char*> instance_extensions)
    {
        VkResult err;

        // Create Vulkan Instance
        {
            VkInstanceCreateInfo create_info = {};
            create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;

            // Enumerate available extensions
            uint32_t properties_count;
            ImVector<VkExtensionProperties> properties;
            vkEnumerateInstanceExtensionProperties(nullptr, &properties_count, nullptr);
            properties.resize(properties_count);
            err = vkEnumerateInstanceExtensionProperties(nullptr, &properties_count, properties.Data);
            check_vk_result(err);

            // Enable required extensions
            if (IsExtensionAvailable(properties, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME))
                instance_extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
#ifdef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
            if (IsExtensionAvailable(properties, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME))
            {
                instance_extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
                create_info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
            }
#endif

            // Enabling validation layers
#ifdef IMGUI_VULKAN_DEBUG_REPORT
        const char* layers[] = { "VK_LAYER_KHRONOS_validation" };
        create_info.enabledLayerCount = 1;
        create_info.ppEnabledLayerNames = layers;
        instance_extensions.push_back("VK_EXT_debug_report");
#endif

            // Create Vulkan Instance
            create_info.enabledExtensionCount = (uint32_t)instance_extensions.Size;
            create_info.ppEnabledExtensionNames = instance_extensions.Data;
            err = vkCreateInstance(&create_info, g_Allocator, &g_Instance);
            check_vk_result(err);

            // Setup the debug report callback
#ifdef IMGUI_VULKAN_DEBUG_REPORT
        auto vkCreateDebugReportCallbackEXT = (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(g_Instance, "vkCreateDebugReportCallbackEXT");
        IM_ASSERT(vkCreateDebugReportCallbackEXT != nullptr);
        VkDebugReportCallbackCreateInfoEXT debug_report_ci = {};
        debug_report_ci.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
        debug_report_ci.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;
        debug_report_ci.pfnCallback = debug_report;
        debug_report_ci.pUserData = nullptr;
        err = vkCreateDebugReportCallbackEXT(g_Instance, &debug_report_ci, g_Allocator, &g_DebugReport);
        check_vk_result(err);
#endif
        }

        // Select Physical Device (GPU)
        g_PhysicalDevice = SetupVulkan_SelectPhysicalDevice();

        // Select graphics queue family
        {
            uint32_t count;
            vkGetPhysicalDeviceQueueFamilyProperties(g_PhysicalDevice, &count, nullptr);
            VkQueueFamilyProperties* queues = (VkQueueFamilyProperties*)malloc(sizeof(VkQueueFamilyProperties) * count);
            vkGetPhysicalDeviceQueueFamilyProperties(g_PhysicalDevice, &count, queues);
            for (uint32_t i = 0; i < count; i++)
                if (queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
                {
                    g_QueueFamily = i;
                    break;
                }
            free(queues);
            IM_ASSERT(g_QueueFamily != (uint32_t)-1);
        }

        // Create Logical Device (with 1 queue)
        {
            ImVector<const char*> device_extensions;
            device_extensions.push_back("VK_KHR_swapchain");

            // Enumerate physical device extension
            uint32_t properties_count;
            ImVector<VkExtensionProperties> properties;
            vkEnumerateDeviceExtensionProperties(g_PhysicalDevice, nullptr, &properties_count, nullptr);
            properties.resize(properties_count);
            vkEnumerateDeviceExtensionProperties(g_PhysicalDevice, nullptr, &properties_count, properties.Data);
#ifdef VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
        if (IsExtensionAvailable(properties, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME))
            device_extensions.push_back(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
#endif

            const float queue_priority[] = {1.0f};
            VkDeviceQueueCreateInfo queue_info[1] = {};
            queue_info[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queue_info[0].queueFamilyIndex = g_QueueFamily;
            queue_info[0].queueCount = 1;
            queue_info[0].pQueuePriorities = queue_priority;
            VkDeviceCreateInfo create_info = {};
            create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
            create_info.queueCreateInfoCount = sizeof(queue_info) / sizeof(queue_info[0]);
            create_info.pQueueCreateInfos = queue_info;
            create_info.enabledExtensionCount = (uint32_t)device_extensions.Size;
            create_info.ppEnabledExtensionNames = device_extensions.Data;
            err = vkCreateDevice(g_PhysicalDevice, &create_info, g_Allocator, &g_Device);
            check_vk_result(err);
            vkGetDeviceQueue(g_Device, g_QueueFamily, 0, &g_Queue);
        }

        // Create Descriptor Pool
        // The example only requires a single combined image sampler descriptor for the font image and only uses one descriptor set (for that)
        // If you wish to load e.g. additional textures you may need to alter pools sizes.
        {
            VkDescriptorPoolSize pool_sizes[] =
            {
                {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
            };
            VkDescriptorPoolCreateInfo pool_info = {};
            pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
            pool_info.maxSets = 1;
            pool_info.poolSizeCount = (uint32_t)IM_ARRAYSIZE(pool_sizes);
            pool_info.pPoolSizes = pool_sizes;
            err = vkCreateDescriptorPool(g_Device, &pool_info, g_Allocator, &g_DescriptorPool);
            check_vk_result(err);
        }
    }

    // All the ImGui_ImplVulkanH_XXX structures/functions are optional helpers used by the demo.
    // Your real engine/app may not use them.
    static void SetupVulkanWindow(ImGuiContext* ctx, ImGui_ImplVulkanH_Window* wd, VkSurfaceKHR surface, int width,
                                  int height)
    {
        wd->Surface = surface;

        // Check for WSI support
        VkBool32 res;
        vkGetPhysicalDeviceSurfaceSupportKHR(g_PhysicalDevice, g_QueueFamily, wd->Surface, &res);
        if (res != VK_TRUE)
        {
            fprintf(stderr, "Error no WSI support on physical device 0\n");
            exit(-1);
        }

        // Select Surface Format
        const VkFormat requestSurfaceImageFormat[] = {
            VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8_UNORM, VK_FORMAT_R8G8B8_UNORM
        };
        const VkColorSpaceKHR requestSurfaceColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
        wd->SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(g_PhysicalDevice, wd->Surface,
                                                                  requestSurfaceImageFormat,
                                                                  (size_t)IM_ARRAYSIZE(requestSurfaceImageFormat),
                                                                  requestSurfaceColorSpace);

        // Select Present Mode
#ifdef IMGUI_UNLIMITED_FRAME_RATE
        VkPresentModeKHR present_modes[] = { VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_FIFO_KHR };
#else
        VkPresentModeKHR present_modes[] = {VK_PRESENT_MODE_FIFO_KHR};
#endif
        wd->PresentMode = ImGui_ImplVulkanH_SelectPresentMode(g_PhysicalDevice, wd->Surface, &present_modes[0],
                                                              IM_ARRAYSIZE(present_modes));
        //printf("[vulkan] Selected PresentMode = %d\n", wd->PresentMode);

        // Create SwapChain, RenderPass, Framebuffer, etc.
        IM_ASSERT(g_MinImageCount >= 2);
        ImGui_ImplVulkanH_CreateOrResizeWindow(ctx, g_Instance, g_PhysicalDevice, g_Device, wd, g_QueueFamily,
                                               g_Allocator, width, height, g_MinImageCount);
    }

    static void CleanupVulkan()
    {
        auto err = vkDeviceWaitIdle(g_Device);
        check_vk_result(err);
        vkDestroyDescriptorPool(g_Device, g_DescriptorPool, g_Allocator);

#ifdef IMGUI_VULKAN_DEBUG_REPORT
        // Remove the debug report callback
        auto vkDestroyDebugReportCallbackEXT = (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(g_Instance, "vkDestroyDebugReportCallbackEXT");
        vkDestroyDebugReportCallbackEXT(g_Instance, g_DebugReport, g_Allocator);
#endif // IMGUI_VULKAN_DEBUG_REPORT

        vkDestroyDevice(g_Device, g_Allocator);
        vkDestroyInstance(g_Instance, g_Allocator);
    }
};
