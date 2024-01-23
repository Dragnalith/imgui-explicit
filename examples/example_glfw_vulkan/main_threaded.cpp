// Dear ImGui: standalone example application for Glfw + Vulkan

// Learn about Dear ImGui:
// - FAQ                  https://dearimgui.com/faq
// - Getting Started      https://dearimgui.com/getting-started
// - Documentation        https://dearimgui.com/docs (same as your local docs/ folder).
// - Introduction, links and more at the top of imgui.cpp

// Important note to the reader who wish to integrate imgui_impl_vulkan.cpp/.h in their own engine/app.
// - Common ImGui_ImplVulkan_XXX functions and structures are used to interface with imgui_impl_vulkan.cpp/.h.
//   You will use those if you want to use this rendering backend in your engine/app.
// - Helper ImGui_ImplVulkanH_XXX functions and structures are only used by this example (main.cpp) and by
//   the backend itself (imgui_impl_vulkan.cpp), but should PROBABLY NOT be used by your own engine/app code.
// Read comments in imgui_impl_vulkan.h.

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"
#include <stdio.h>          // printf, fprintf
#include <stdlib.h>         // abort
#include <thread>
#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>
#define MULTI_THREADED
#include "single_context.h"

// [Win32] Our example includes a copy of glfw3.lib pre-compiled with VS2010 to maximize ease of testing and compatibility with old VS compilers.
// To link with VS2010-era libraries, VS2015+ requires linking with legacy_stdio_definitions.lib, which we do using this pragma.
// Your own project should not be affected, as you are likely to link with a newer binary of GLFW that is adequate for your version of Visual Studio.
#if defined(_MSC_VER) && (_MSC_VER >= 1900) && !defined(IMGUI_DISABLE_WIN32_FUNCTIONS)
#pragma comment(lib, "legacy_stdio_definitions")
#endif

//#define IMGUI_UNLIMITED_FRAME_RATE
#ifdef _DEBUG
#define IMGUI_VULKAN_DEBUG_REPORT
#endif

// Data
VkAllocationCallbacks* ThreadWindow::g_Allocator = nullptr;
VkInstance ThreadWindow::g_Instance = VK_NULL_HANDLE;
VkPhysicalDevice ThreadWindow::g_PhysicalDevice = VK_NULL_HANDLE;
VkDevice ThreadWindow::g_Device = VK_NULL_HANDLE;
uint32_t ThreadWindow::g_QueueFamily = (uint32_t)-1;
VkQueue ThreadWindow::g_Queue = VK_NULL_HANDLE;
// static VkDebugReportCallbackEXT ThreadWindow::g_DebugReport = VK_NULL_HANDLE;
VkPipelineCache ThreadWindow::g_PipelineCache = VK_NULL_HANDLE;
VkDescriptorPool ThreadWindow::g_DescriptorPool = VK_NULL_HANDLE;

int ThreadWindow::g_MinImageCount = 2;

int ThreadWindow::nActive = 0;

JobQueue glfQueue;

static void glfw_error_callback(int error, const char* description)
{
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

void awakenGlfw()
{
    glfwPostEmptyEvent();
}

#ifdef IMGUI_VULKAN_DEBUG_REPORT
static VKAPI_ATTR VkBool32 VKAPI_CALL debug_report(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objectType, uint64_t object, size_t location, int32_t messageCode, const char* pLayerPrefix, const char* pMessage, void* pUserData)
{
    (void)flags; (void)object; (void)location; (void)messageCode; (void)pUserData; (void)pLayerPrefix; // Unused arguments
    fprintf(stderr, "[vulkan] Debug report from ObjectType: %i\nMessage: %s\n\n", objectType, pMessage);
    return VK_FALSE;
}
#endif // IMGUI_VULKAN_DEBUG_REPORT

// Main code
int main(int, char**)
{
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
        return 1;

    // Create window with Vulkan context
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    // GLFWwindow* window = glfwCreateWindow(1280, 720, "Dear ImGui GLFW+Vulkan example", nullptr, nullptr);
    if (!glfwVulkanSupported())
    {
        printf("GLFW: Vulkan Not Supported\n");
        return 1;
    }

    ImVector<const char*> extensions;
    uint32_t extensions_count = 0;
    const char** glfw_extensions = glfwGetRequiredInstanceExtensions(&extensions_count);
    for (uint32_t i = 0; i < extensions_count; i++)
        extensions.push_back(glfw_extensions[i]);

    ThreadWindow::SetupVulkan(extensions);

    const int numWindows = 3;

    std::vector<std::thread> threads;
    threads.reserve(numWindows);
    for (int i = 0; i < numWindows; ++i)
        threads.emplace_back([&]
        {
            ThreadWindow window;
            window.fullSetup();
            while (window.oneFrame());
            window.close();
        });

    while (ThreadWindow::nActive > 0)
    {
        // glfwPollEvents();
        glfwWaitEvents();
        while (!glfQueue.jobs.empty())
        {
            std::function<void()> job;
            {
                std::lock_guard<std::mutex> lg{glfQueue.m};
                job = glfQueue.jobs.front();
                glfQueue.jobs.pop();
            }
            job();
        }
    }
    for (auto& t : threads)
        t.join();

    ThreadWindow::CleanupVulkan();

    glfwTerminate();

    return 0;
}
