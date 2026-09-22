// AndroidShell.cpp — NativeActivity glue and complete PS1 runtime engine for Sheep Raider on Android.
#include <jni.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android/input.h>
#include <android/keycodes.h>
#include <android_native_app_glue.h>
#include <pthread.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES3/gl3.h>
#include <aaudio/AAudio.h>
#include <cstdarg>

#define LOG_TAG "WE2002"

static FILE* s_diag_file = nullptr;

static void InitDiagLog(const char* extDir, const char* filesDir) {
    if (s_diag_file) return;
    s_diag_file = fopen("/storage/emulated/0/Download/we2002_log.txt", "w");
    if (!s_diag_file && extDir) {
        std::string p = std::string(extDir) + "/we2002_log.txt";
        s_diag_file = fopen(p.c_str(), "w");
    }
    if (!s_diag_file && filesDir) {
        std::string p = std::string(filesDir) + "/we2002_log.txt";
        s_diag_file = fopen(p.c_str(), "w");
    }
}

static void DiagLogWrite(int prio, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    __android_log_vprint(prio, LOG_TAG, fmt, ap);
    va_end(ap);

    if (s_diag_file) {
        va_start(ap, fmt);
        vfprintf(s_diag_file, fmt, ap);
        fprintf(s_diag_file, "\n");
        fflush(s_diag_file);
        va_end(ap);
    }
}

#define LOGI(...) DiagLogWrite(ANDROID_LOG_INFO, __VA_ARGS__)
#define LOGE(...) DiagLogWrite(ANDROID_LOG_ERROR, __VA_ARGS__)

#include "psx_bios_backend.h"
#include "cpu_state.h"
#include "psx_scheduler.h"
#include "psx_cycles.h"
#include "bios_hle.h"
#include "gpu.h"
#include "gpu_render.h"
#include "gpu_sw_renderer.h"
#include "sio.h"
#include "spu.h"
#include "fntrace.h"
#include "cdrom.h"
#include "memcard.h"

// Pull native_app_glue anchor
extern "C" void ANativeActivity_onCreate(ANativeActivity*, void*, std::size_t);
[[maybe_unused]] static const void* const kGlueAnchor =
    reinterpret_cast<const void*>(&ANativeActivity_onCreate);

// Controller state variables (PS1 button bitmask: active-low)
// Bit 0: Select, 3: Start, 4: Up, 5: Right, 6: Down, 7: Left
// Bit 8: L2, 9: R2, 10: L1, 11: R1, 12: Triangle, 13: Circle, 14: Cross, 15: Square
static volatile uint16_t g_pad_buttons = 0xFFFF;
static volatile int16_t g_stick_x = 0;
static volatile int16_t g_stick_y = 0;
static volatile bool g_has_physical_pad = false;

// Embedded assets from openbios_embedded.c and game_text_embedded.c
extern "C" {
    extern const unsigned char g_embedded_openbios[524288];
    extern const unsigned char g_embedded_game_text[184320];

    // Core runtime initialization prototypes
    void memory_init(const char* bios_path);
    void memory_set_sr_ptr(const uint32_t* p);
    void psx_irq_set_cause_ptr(uint32_t* p);
    void dma_init(void);
    void mdec_init(void);
    void timers_init(void);
    void interrupts_init(void);
    void sio_init(void);
    void spu_init(void);
    void cdrom_init(const char* cue_path);
    int  cdrom_has_disc(void);
    void psx_cycles_reset_for_boot(void);
    void starvation_ring_reset(void) {}
    void present_session_reset(void) {}
    void psx_netplay_cold_reset(void) {}
    void psx_icache_reset(void);
    void dirty_ram_register_text_image(uint32_t phys_lo, const uint8_t *bytes, uint32_t len);

    uint32_t psx_read_word(uint32_t addr);
    void psx_write_word(uint32_t addr, uint32_t val);
    uint16_t psx_read_half(uint32_t addr);
    void psx_write_half(uint32_t addr, uint16_t val);
    uint8_t psx_read_byte(uint32_t addr);
    void psx_write_byte(uint32_t addr, uint8_t val);

    // BIOS Registry with OpenBIOS backend
    extern const PsxBiosBackend OpenBIOS_psx_bios_backend;
    const PsxBiosBackend *const psx_bios_registry[] = {
        &OpenBIOS_psx_bios_backend
    };
    const uint32_t psx_bios_registry_count = 1u;

    // Standalone stubs for netplay and debug tracking
    int psx_netplay_active(void) { return 0; }
    int psx_netplay_is_host(void) { return 1; }
    uint32_t psx_netplay_rb_sticky_bb_pc = 0;

    int g_psx_cps_mode = 0;
    uint32_t g_debug_current_func_addr = 0;
    uint32_t g_debug_last_store_pc = 0;
    volatile uint32_t g_psx_last_fn_entry = 0;
    uint64_t g_dispatch_static_hits = 0;
    uint64_t s_frame_count = 0;

    void debug_server_trace_dispatch(uint32_t) {}
    void mod_runtime_on_dispatch(uint32_t) {}
    void text_xlate_on_dispatch(uint32_t) {}
    int parity_trace_is_armed(void) { return 0; }
    void parity_trace_record(uint32_t) {}

    void event_ring_record_aux(uint16_t, uint8_t, uint32_t) {}
    void debug_server_poll(void) {}
    void debug_server_log_sio_write(uint32_t, uint32_t, uint8_t) {}
    void starvation_ring_record(uint8_t, uint8_t, uint8_t, uint16_t, uint16_t, int, int, int, int, int, uint8_t, uint32_t, uint8_t, uint8_t, uint8_t, uint8_t, int) {}
    void audio_trace_pcm(int, const int16_t*, int) {}
    void audio_trace_event(uint16_t, uint32_t, uint32_t) {}
    void psx_frontend_on_savestate_notify(int, int, int) {}
    void psx_frontend_on_savestate_loaded(void) {}

    int g_call_unit_depth = 0;
    int g_ws_bd_stretch_on = 0;
    int g_ws_bd_stretch_pct = 0;

    int psx_netplay_cd_bisect_active(void) { return 0; }
    uint32_t psx_netplay_sim_tick(void) { return 0; }
    int psx_netplay_is_resimulating(void) { return 0; }

    void event_ring_record(uint16_t, uint8_t) {}
    const void* gl_backend_get(void) { return nullptr; }
    const void* vk_backend_get(void) { return nullptr; }

    void overlay_capture_before_dma(uint32_t, uint32_t) {}
    void overlay_capture_on_dma(uint32_t, uint32_t, const uint8_t*) {}
    int overlay_fp_enabled(void) { return 0; }
    int overlay_loader_is_candidate(uint32_t) { return 0; }
    void overlay_regs_snap(uint32_t[34], const void*) {}
    int overlay_loader_dispatch(void*, uint32_t) { return 0; }
    uint32_t psx_overlay_resident_crc_at(uint32_t) { return 0; }
    void overlay_loader_shadow_scheduler_escape_fixup(void) {}

    void psx_mod_function_entry(void*, uint32_t) {}
    void debug_server_log_thread_event(uint32_t, void*, uint32_t, uint32_t, uint32_t) {}

    CPUState *debug_cpu_ptr = nullptr;

    int psx_netplay_rb_recover_null_pc(void*, uint32_t*) { return 0; }
    int overlay_loader_shadow_native_thread_switch_bail(void) { return 0; }
    void overlay_loader_shadow_escape_fixup(uint64_t) {}
    void psx_unknown_dispatch_record(uint32_t pc, uint32_t ra, uint32_t target, uint32_t sp) {
        LOGE("UNKNOWN DISPATCH: pc=0x%08X ra=0x%08X target=0x%08X sp=0x%08X", pc, ra, target, sp);
    }
    void debug_server_wait_if_paused(void) {}
    void overlay_fp_log(uint32_t, const uint32_t*, const void*, int) {}
    int overlay_loader_call_native(void*, uint32_t) { return 0; }
    void debug_server_log_restore_event(uint32_t, uint32_t, uint32_t) {}
    void psx_netplay_poll_snap(void) {}
    void psx_selfcheck_poll(void) {}
    void psx_rewind_poll(void) {}
    int psx_selfcheck_enabled(void) { return 0; }
    void starvation_watchdog_check(void) {}
    void starvation_ring_pc_sample(void) {}

    void psx_fatal_halt(const char* msg) {
        LOGE("FATAL HALT: %s", msg ? msg : "unknown");
        FILE* cf = fopen("psx_crash.txt", "w");
        if (cf) {
            fprintf(cf, "FATAL HALT: %s\n", msg ? msg : "unknown");
            fclose(cf);
        }
        usleep(300000);
        abort();
    }
    void psx_crash_trace_dump(const char* reason) {
        LOGE("CRASH TRACE: %s", reason ? reason : "unknown");
    }
    uint64_t crash_trace_dispatch_seq_get(void) { return 0; }
    uint32_t crash_trace_dispatch_ring_get(int) { return 0; }
    void device_trace_note(uint32_t, uint32_t) {}
    void mod_runtime_on_vblank(void) {}

    int g_ram_read_watch_active = 0;
    void (*g_overlay_flush_pending_cycles)(void) = nullptr;

    void debug_server_trace_ram_read_watch(uint32_t, uint32_t) {}
    void debug_server_trace_write_check(uint32_t, uint32_t, uint32_t, uint8_t) {}
    void parity_trace_note_write(uint32_t, uint32_t, uint32_t) {}
    void overlay_loader_active_write_check(uint32_t, uint32_t) {}
    void debug_server_trace_mmio_read(uint32_t, uint32_t, uint8_t) {}
    void debug_server_trace_mmio_write(uint32_t, uint32_t, uint8_t) {}

    int gl_renderer_texture_banks_supported(void) { return 0; }
    int gl_renderer_select_texture_bank(uint16_t) { return 0; }
    void text_xlate_vram_upload(int, int, int, int) {}
    uint16_t mod_texture_packet_bank(uint32_t, const uint32_t*, uint32_t) { return 0; }
    int mod_texture_packet_precision(uint32_t, float[3], float[6]) { return 0; }

    uint32_t debug_guest_ra(void) { return 0; }
    uint32_t debug_guest_sp(void) { return 0; }
    void overlay_loader_note_code_write(void) {}
    void overlay_loader_resync_validation_after_restore(void) {}

    // JNI input bridge
    JNIEXPORT void JNICALL
    Java_com_otaviomorais_sheepraider_hud_HudBridge_nativePostButton(
        JNIEnv*, jclass, jint button, jboolean down) {
        uint16_t mask = 0;
        switch (button) {
            case 0: mask = (1 << 14); break; // Cross (✕)
            case 1: mask = (1 << 13); break; // Circle (○)
            case 2: mask = (1 << 15); break; // Square (▢)
            case 3: mask = (1 << 12); break; // Triangle (△)
            case 4: mask = (1 << 10); break; // L1
            case 5: mask = (1 << 11); break; // R1
            case 6: mask = (1 << 0);  break; // Select
            case 7: mask = (1 << 3);  break; // Start
            case 8: mask = (1 << 8);  break; // L2
            case 9: mask = (1 << 9);  break; // R2
            default: break;
        }
        if (mask != 0) {
            if (down) {
                g_pad_buttons &= ~mask;
            } else {
                g_pad_buttons |= mask;
            }
        }
    }

    JNIEXPORT void JNICALL
    Java_com_otaviomorais_sheepraider_hud_HudBridge_nativePostAxis(
        JNIEnv*, jclass, jint axis, jfloat value) {
        if (axis == 0) {
            g_stick_x = static_cast<int16_t>(value * 32767.0f);
            if (value < -0.15f) g_pad_buttons &= ~(1 << 7); else g_pad_buttons |= (1 << 7); // Left
            if (value > 0.15f)  g_pad_buttons &= ~(1 << 5); else g_pad_buttons |= (1 << 5); // Right
        } else if (axis == 1) {
            g_stick_y = static_cast<int16_t>(value * 32767.0f);
            if (value < -0.15f) g_pad_buttons &= ~(1 << 4); else g_pad_buttons |= (1 << 4); // Up
            if (value > 0.15f)  g_pad_buttons &= ~(1 << 6); else g_pad_buttons |= (1 << 6); // Down
        }
    }

    JNIEXPORT void JNICALL
    Java_com_otaviomorais_sheepraider_hud_HudBridge_nativePostConnected(
        JNIEnv*, jclass, jboolean) {}

    JNIEXPORT jint JNICALL
    Java_com_otaviomorais_sheepraider_hud_HudBridge_nativePollContext(JNIEnv*, jclass) {
        return 1; // Always show gameplay HUD when running
    }

    JNIEXPORT void JNICALL
    Java_com_otaviomorais_sheepraider_hud_HudBridge_nativeSetPhysicalGamepad(
        JNIEnv*, jclass, jboolean connected) {
        g_has_physical_pad = (connected == JNI_TRUE);
    }

    JNIEXPORT void JNICALL
    Java_com_otaviomorais_sheepraider_hud_HudBridge_nativeSetGhostPadDeviceIds(
        JNIEnv*, jclass, jintArray) {}
} // extern "C"

// --- Global Engine & Window State ---
static struct android_app* g_app = nullptr;
static ANativeWindow* g_window = nullptr;
static pthread_t g_game_thread = 0;
static volatile bool g_running = true;
static volatile bool g_window_ready = false;

// EGL & GLES state
static EGLDisplay s_egl_display = EGL_NO_DISPLAY;
static EGLSurface s_egl_surface = EGL_NO_SURFACE;
static EGLContext s_egl_context = EGL_NO_CONTEXT;
static GLuint s_program = 0;
static GLuint s_tex = 0;
static GLuint s_vbo = 0;
static uint32_t s_pixels[1024 * 512];

// Audio state
static AAudioStream* s_audio_stream = nullptr;

static const char* kVertexShader =
    "attribute vec2 a_pos;\n"
    "attribute vec2 a_uv;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "    gl_Position = vec4(a_pos, 0.0, 1.0);\n"
    "    v_uv = a_uv;\n"
    "}\n";

static const char* kFragmentShader =
    "precision mediump float;\n"
    "varying vec2 v_uv;\n"
    "uniform sampler2D u_tex;\n"
    "void main() {\n"
    "    vec4 c = texture2D(u_tex, v_uv);\n"
    "    gl_FragColor = vec4(c.b, c.g, c.r, 1.0);\n" // Little-endian ARGB -> RGBA
    "}\n";

static GLuint CompileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[512];
        glGetShaderInfoLog(s, sizeof(buf), nullptr, buf);
        LOGE("Shader compile error: %s", buf);
    }
    return s;
}

static bool InitEgl(ANativeWindow* window) {
    if (!window) {
        LOGE("InitEgl: window is null");
        return false;
    }

    s_egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (s_egl_display == EGL_NO_DISPLAY) {
        LOGE("InitEgl: eglGetDisplay failed (0x%04x)", eglGetError());
        return false;
    }

    if (!eglInitialize(s_egl_display, nullptr, nullptr)) {
        LOGE("InitEgl: eglInitialize failed (0x%04x)", eglGetError());
        return false;
    }

    const EGLint attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_BLUE_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_RED_SIZE, 8,
        EGL_NONE
    };

    EGLConfig config = nullptr;
    EGLint num_configs = 0;
    eglChooseConfig(s_egl_display, attribs, &config, 1, &num_configs);
    if (num_configs == 0 || !config) {
        LOGI("InitEgl: ES3 config unavailable, trying ES2 config...");
        const EGLint attribs_es2[] = {
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_BLUE_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_RED_SIZE, 8,
            EGL_NONE
        };
        eglChooseConfig(s_egl_display, attribs_es2, &config, 1, &num_configs);
        if (num_configs == 0 || !config) {
            LOGE("InitEgl: eglChooseConfig returned 0 configs (0x%04x)", eglGetError());
            return false;
        }
    }

    // Set buffer geometry to match EGLConfig format on Android
    EGLint format = 0;
    if (eglGetConfigAttrib(s_egl_display, config, EGL_NATIVE_VISUAL_ID, &format)) {
        ANativeWindow_setBuffersGeometry(window, 0, 0, format);
        LOGI("InitEgl: ANativeWindow buffer geometry set with format %d", format);
    }

    s_egl_surface = eglCreateWindowSurface(s_egl_display, config,
                                           static_cast<EGLNativeWindowType>(window),
                                           nullptr);
    if (s_egl_surface == EGL_NO_SURFACE) {
        LOGE("InitEgl: eglCreateWindowSurface failed (0x%04x)", eglGetError());
        return false;
    }

    const EGLint ctx_attribs_es3[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE
    };
    s_egl_context = eglCreateContext(s_egl_display, config, EGL_NO_CONTEXT, ctx_attribs_es3);
    if (s_egl_context == EGL_NO_CONTEXT) {
        LOGI("InitEgl: GLES3 context creation failed, trying GLES2 context...");
        const EGLint ctx_attribs_es2[] = {
            EGL_CONTEXT_CLIENT_VERSION, 2,
            EGL_NONE
        };
        s_egl_context = eglCreateContext(s_egl_display, config, EGL_NO_CONTEXT, ctx_attribs_es2);
        if (s_egl_context == EGL_NO_CONTEXT) {
            LOGE("InitEgl: eglCreateContext failed for both GLES3 and GLES2 (0x%04x)", eglGetError());
            return false;
        }
    }

    if (!eglMakeCurrent(s_egl_display, s_egl_surface, s_egl_surface, s_egl_context)) {
        LOGE("InitEgl: eglMakeCurrent failed (0x%04x)", eglGetError());
        return false;
    }

    // Build GLES pipeline
    GLuint vs = CompileShader(GL_VERTEX_SHADER, kVertexShader);
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, kFragmentShader);
    s_program = glCreateProgram();
    glAttachShader(s_program, vs);
    glAttachShader(s_program, fs);
    glLinkProgram(s_program);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint linked = 0;
    glGetProgramiv(s_program, GL_LINK_STATUS, &linked);
    if (!linked) {
        char buf[512];
        glGetProgramInfoLog(s_program, sizeof(buf), nullptr, buf);
        LOGE("Shader link error: %s", buf);
        return false;
    }

    glGenTextures(1, &s_tex);
    glBindTexture(GL_TEXTURE_2D, s_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1024, 512, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Setup VAO if supported (required by some GLES3 drivers)
    typedef void (*PFNGLGENVERTEXARRAYSOESPROC)(GLsizei, GLuint*);
    typedef void (*PFNGLBINDVERTEXARRAYOESPROC)(GLuint);
    auto pfnGenVAO = (PFNGLGENVERTEXARRAYSOESPROC)eglGetProcAddress("glGenVertexArrays");
    if (!pfnGenVAO) pfnGenVAO = (PFNGLGENVERTEXARRAYSOESPROC)eglGetProcAddress("glGenVertexArraysOES");
    auto pfnBindVAO = (PFNGLBINDVERTEXARRAYOESPROC)eglGetProcAddress("glBindVertexArray");
    if (!pfnBindVAO) pfnBindVAO = (PFNGLBINDVERTEXARRAYOESPROC)eglGetProcAddress("glBindVertexArrayOES");
    GLuint vao = 0;
    if (pfnGenVAO && pfnBindVAO) {
        pfnGenVAO(1, &vao);
        pfnBindVAO(vao);
    }

    glGenBuffers(1, &s_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    glBufferData(GL_ARRAY_BUFFER, 16 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);

    GLint pos_loc = glGetAttribLocation(s_program, "a_pos");
    GLint uv_loc = glGetAttribLocation(s_program, "a_uv");
    if (pos_loc >= 0) {
        glEnableVertexAttribArray(pos_loc);
        glVertexAttribPointer(pos_loc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    }
    if (uv_loc >= 0) {
        glEnableVertexAttribArray(uv_loc);
        glVertexAttribPointer(uv_loc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    }

    LOGI("EGL and GLES pipeline successfully initialized!");
    return true;
}

static void InitAudio(void) {
    AAudioStreamBuilder* builder = nullptr;
    if (AAudio_createStreamBuilder(&builder) == AAUDIO_OK) {
        AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
        AAudioStreamBuilder_setChannelCount(builder, 2);
        AAudioStreamBuilder_setSampleRate(builder, 44100);
        AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
        if (AAudioStreamBuilder_openStream(builder, &s_audio_stream) == AAUDIO_OK) {
            AAudioStream_requestStart(s_audio_stream);
            LOGI("AAudio stream initialized at 44100Hz stereo.");
        }
        AAudioStreamBuilder_delete(builder);
    }
}

static void AndroidVblankCallback(void) {
    if (!g_running || (g_app && g_app->destroyRequested)) {
        return;
    }

    // 1. Controller input
    sio_set_pad_state(g_pad_buttons);
    uint8_t lx = (uint8_t)(((int32_t)g_stick_x + 32768) >> 8);
    uint8_t ly = (uint8_t)(((int32_t)g_stick_y + 32768) >> 8);
    sio_set_pad_sticks(0, lx, ly, 0x80, 0x80);

    // 2. Audio tick (44100 / 60 = 735 stereo samples)
    int16_t spu_samples[735 * 2];
    spu_render(spu_samples, 735);
    if (s_audio_stream) {
        AAudioStream_write(s_audio_stream, spu_samples, 735, 0);
    }

    // 3. Video present
    GpuDisplayInfo di;
    gpu_get_display_info(&di);

    s_frame_count++;
    static int s_prev_started = 0;
    int started = fntrace_is_game_started();
    if (started != s_prev_started) {
        s_prev_started = started;
        LOGI("*** GAME EXECUTION STARTED! (s_frame_count=%llu, started=%d) ***",
             (unsigned long long)s_frame_count, started);
    }
    if (s_frame_count % 60 == 1) {
        LOGI("VBlank frame #%llu: game_started=%d, di.disabled=%d, w=%u, h=%u, x=%d, y=%d, depth24=%d",
             (unsigned long long)s_frame_count, started,
             di.disabled, di.width, di.height, di.display_x, di.display_y, di.depth24);
    }

    if (s_egl_display != EGL_NO_DISPLAY && s_egl_surface != EGL_NO_SURFACE) {
        EGLint win_w = 0, win_h = 0;
        eglQuerySurface(s_egl_display, s_egl_surface, EGL_WIDTH, &win_w);
        eglQuerySurface(s_egl_display, s_egl_surface, EGL_HEIGHT, &win_h);

        if (win_w > 0 && win_h > 0) {
            if (di.disabled || di.width == 0 || di.height == 0) {
                glViewport(0, 0, win_w, win_h);
                glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT);
                eglSwapBuffers(s_egl_display, s_egl_surface);
                return;
            }

            uint32_t w = di.width > 1024 ? 1024 : di.width;
            uint32_t h = di.height > 512 ? 512 : di.height;

            if (di.depth24) {
                for (uint32_t row = 0; row < h; row++) {
                    gpu_depth24_present_row(&di, row, s_pixels + row * w, w);
                }
            } else {
                sw_render_display(s_pixels, w * sizeof(uint32_t), di.display_x, di.display_y, w, h);
            }

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, s_tex);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, s_pixels);

            // Compute 4:3 letterboxing
            int target_w = win_w;
            int target_h = (win_w * 3) / 4;
            if (target_h > win_h) {
                target_h = win_h;
                target_w = (win_h * 4) / 3;
            }
            int vp_x = (win_w - target_w) / 2;
            int vp_y = (win_h - target_h) / 2;

            glViewport(0, 0, win_w, win_h);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);

            glViewport(vp_x, vp_y, target_w, target_h);

            float u_max = (float)w / 1024.0f;
            float v_max = (float)h / 512.0f;
            float quad[16] = {
                -1.0f, -1.0f,  0.0f,  v_max,
                 1.0f, -1.0f,  u_max, v_max,
                -1.0f,  1.0f,  0.0f,  0.0f,
                 1.0f,  1.0f,  u_max, 0.0f
            };

            glUseProgram(s_program);
            glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
            glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(quad), quad);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

            eglSwapBuffers(s_egl_display, s_egl_surface);
        }
    }
}

static std::string FindDiscPath(const char* filesDir) {
    std::vector<std::string> searchDirs;
    if (filesDir) {
        searchDirs.push_back(std::string(filesDir) + "/discimage");
        searchDirs.push_back(std::string(filesDir));
    }
    searchDirs.push_back("discimage");
    searchDirs.push_back("/storage/emulated/0/Download/Sheep_Raider_Disc");
    searchDirs.push_back("/storage/emulated/0/Download");
    searchDirs.push_back("/sdcard/Download/Sheep_Raider_Disc");
    searchDirs.push_back("/sdcard/Download");
    searchDirs.push_back("/storage/emulated/0/Android/data/com.otaviomorais.sheepraider/files/discimage");
    searchDirs.push_back("/storage/emulated/0/Android/data/com.otaviomorais.sheepraider/files");

    // Scan directories for .cue or .bin/.iso
    for (const auto& dir : searchDirs) {
        DIR* dp = opendir(dir.c_str());
        if (!dp) continue;
        struct dirent* ep;
        std::string found_cue = "";
        std::string found_bin = "";
        while ((ep = readdir(dp)) != nullptr) {
            std::string name = ep->d_name;
            std::string lower = name;
            for (char& c : lower) c = tolower(c);
            if (lower.find(".cue") != std::string::npos) {
                if (found_cue.empty() || lower.find("sheep") != std::string::npos || lower.find("slus") != std::string::npos) {
                    found_cue = dir + "/" + name;
                }
            }
            if (lower.find(".bin") != std::string::npos || lower.find(".iso") != std::string::npos) {
                if (found_bin.empty() || lower.find("game.bin") != std::string::npos || lower.find("sheep") != std::string::npos || lower.find("slus") != std::string::npos) {
                    found_bin = dir + "/" + name;
                }
            }
        }
        closedir(dp);
        if (!found_cue.empty()) {
            LOGI("Found game CUE disc at: %s", found_cue.c_str());
            return found_cue;
        }
        if (!found_bin.empty()) {
            LOGI("Found game BIN disc at: %s", found_bin.c_str());
            return found_bin;
        }
    }

    // Direct fallback candidate paths
    const char* direct_paths[] = {
        "discimage/game.bin",
        "discimage/Winning Eleven 2002 (PS1) World Cup 2002.cue",
        "discimage/Winning Eleven 2002 (PS1) World Cup 2002.BIN",
        "/storage/emulated/0/Download/Winning Eleven 2002 (PS1) World Cup 2002/Winning Eleven 2002 (PS1) World Cup 2002.cue",
        "/storage/emulated/0/Download/Winning Eleven 2002 (PS1) World Cup 2002/Winning Eleven 2002 (PS1) World Cup 2002.BIN",
        "/sdcard/Download/Winning Eleven 2002 (PS1) World Cup 2002/Winning Eleven 2002 (PS1) World Cup 2002.cue",
        "/sdcard/Download/Winning Eleven 2002 (PS1) World Cup 2002/Winning Eleven 2002 (PS1) World Cup 2002.BIN",
        "/storage/emulated/0/Download/Winning Eleven 2002.cue",
        "/storage/emulated/0/Download/Winning Eleven 2002.bin",
    };
    for (const char* p : direct_paths) {
        if (access(p, R_OK) == 0) {
            LOGI("Found valid game disc at direct path: %s", p);
            return p;
        }
    }
    LOGE("No disc image found in any candidate path!");
    return "";
}

static void* GameThreadFunc(void*) {
    const char* filesDir = g_app && g_app->activity ? g_app->activity->internalDataPath : nullptr;
    const char* extDir = g_app && g_app->activity ? g_app->activity->externalDataPath : nullptr;
    InitDiagLog(extDir, filesDir);

    LOGI("=== WINNING ELEVEN 2002 GAME THREAD STARTED ===");
    LOGI("Internal data path: %s", filesDir ? filesDir : "<null>");
    LOGI("External data path: %s", extDir ? extDir : "<null>");

    ANativeWindow* win = g_window;
    if (!win) {
        LOGE("Game thread aborted: window is null");
        return nullptr;
    }

    if (!InitEgl(win)) {
        LOGE("Failed to initialize EGL on game thread");
        if (g_app && !g_app->destroyRequested) {
            ANativeActivity_finish(g_app->activity);
        }
        return nullptr;
    }

    InitAudio();

    // Setup BIOS file
    std::string bios_path = std::string(filesDir ? filesDir : ".") + "/openbios.bin";
    FILE* check_bios = fopen(bios_path.c_str(), "rb");
    bool needs_write = false;
    if (!check_bios) {
        needs_write = true;
    } else {
        fseek(check_bios, 0, SEEK_END);
        if (ftell(check_bios) != 524288) needs_write = true;
        fclose(check_bios);
    }

    if (needs_write) {
        FILE* out_bios = fopen(bios_path.c_str(), "wb");
        if (out_bios) {
            fwrite(g_embedded_openbios, 1, sizeof(g_embedded_openbios), out_bios);
            fclose(out_bios);
            LOGI("Wrote embedded OpenBIOS to %s", bios_path.c_str());
        } else {
            LOGE("Failed to write %s, will fallback to embedded OpenBIOS directly", bios_path.c_str());
        }
    }

    // Arm dirty-RAM text image guard with embedded SLPM_870.56 text (0x80010000, 335872 bytes)
    uint8_t* text_copy = (uint8_t*)malloc(sizeof(g_embedded_game_text));
    if (text_copy) {
        memcpy(text_copy, g_embedded_game_text, sizeof(g_embedded_game_text));
        dirty_ram_register_text_image(0x80010000u & 0x1FFFFFFFu, text_copy, sizeof(g_embedded_game_text));
        LOGI("Armed dirty_ram text image guard for SLPM_870.56 (0x80010000, %zu bytes)", sizeof(g_embedded_game_text));
    }

    std::string disc_path = FindDiscPath(filesDir);
    LOGI("Disc path resolved to: '%s'", disc_path.c_str());

    // PS1 system boot reset sequence
    psx_cycles_reset_for_boot();
    starvation_ring_reset();
    present_session_reset();
    psx_netplay_cold_reset();

    psx_bios_activate(&OpenBIOS_psx_bios_backend);
    // boot_skip = 1: bypass OpenBIOS interactive shell and immediately boot game from disc
    psx_bios_hle_configure(0, 1);
    LOGI("OpenBIOS activated: call_hle=0, boot_skip=1 (boot_skip_enabled=%d)",
         psx_bios_hle_boot_skip_enabled());

    LOGI("Initializing PS1 memory subsystem with BIOS: %s", bios_path.c_str());
    memory_init(bios_path.c_str());

    gr_set_backend(GR_BACKEND_SOFTWARE);
    gpu_init();
    sw_renderer_set_scale(1);

    dma_init();
    mdec_init();
    timers_init();
    interrupts_init();
    sio_init();

    sio_set_pad_connected(0, 1);
    sio_set_pad_connected(1, 0);
    sio_set_pad_analog(0, 0, 0x80, 0x80, 0x80, 0x80);

    // Initialize Memory Card in saves folder
    std::string saves_dir = std::string(filesDir ? filesDir : ".") + "/saves";
    mkdir(saves_dir.c_str(), 0755);
    memcard_init(saves_dir.c_str());
    LOGI("Memcard initialized in: %s", saves_dir.c_str());

    spu_init();

    LOGI("Initializing CD-ROM subsystem with disc: %s", disc_path.c_str());
    cdrom_init(disc_path.empty() ? nullptr : disc_path.c_str());
    int mounted = cdrom_has_disc();
    LOGI("cdrom_has_disc() = %d", mounted);
    if (!mounted) {
        LOGE("CRITICAL: CD-ROM drive has no disc mounted! Check disc path: '%s'", disc_path.c_str());
    }

    cdrom_set_disc_scex("SCEI");
    cdrom_set_game_speed(1);

    // Arm game entry range for SLPM-87056 (entry_pc = 0x80010008)
    const uint32_t kGameEntryPc = 0x80010008u;
    fntrace_set_game_range(kGameEntryPc, 0);
    LOGI("Armed fntrace_set_game_range(0x%08X, 0)", kGameEntryPc);

    gpu_set_vblank_callback(AndroidVblankCallback);

    // Initialize R3000A CPU
    CPUState cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.ld_which_t = 0x20;
    psx_icache_reset();
    cpu.read_word  = psx_read_word;
    cpu.write_word = psx_write_word;
    cpu.read_half  = psx_read_half;
    cpu.write_half = psx_write_half;
    cpu.read_byte  = psx_read_byte;
    cpu.write_byte = psx_write_byte;

    cpu.pc = 0xBFC00000u;
    cpu.cop0[12] = 0x00400000u; // BEV = 1

    memory_set_sr_ptr(&cpu.cop0[12]);
    psx_irq_set_cause_ptr(&cpu.cop0[13]);
    debug_cpu_ptr = &cpu;

    LOGI("PS1 execution loop entering psx_scheduler_run(&cpu)...");
    psx_scheduler_run(&cpu);

    LOGI("psx_scheduler_run returned. Game thread finished.");
    if (g_app && !g_app->destroyRequested) {
        ANativeActivity_finish(g_app->activity);
    }
    return nullptr;
}

static void HandleCommand(android_app* app, int32_t cmd) {
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            if (app->window) {
                ANativeWindow_acquire(app->window);
                g_window = app->window;
                g_window_ready = true;
                LOGI("APP_CMD_INIT_WINDOW: window acquired (%p)", g_window);
            }
            break;
        case APP_CMD_TERM_WINDOW:
            LOGI("APP_CMD_TERM_WINDOW received");
            g_window_ready = false;
            if (g_window) {
                ANativeWindow_release(g_window);
                g_window = nullptr;
            }
            break;
        case APP_CMD_DESTROY:
            LOGI("APP_CMD_DESTROY received");
            g_running = false;
            g_window_ready = false;
            break;
        default:
            break;
    }
}

static int32_t HandleInput(android_app*, AInputEvent* event) {
    int32_t type = AInputEvent_getType(event);
    if (type == AINPUT_EVENT_TYPE_KEY) {
        int32_t keyCode = AKeyEvent_getKeyCode(event);
        int32_t action = AKeyEvent_getAction(event);
        bool down = (action == AKEY_EVENT_ACTION_DOWN);
        uint16_t mask = 0;
        switch (keyCode) {
            case AKEYCODE_BUTTON_A: case AKEYCODE_DPAD_CENTER: mask = (1 << 14); break; // Cross
            case AKEYCODE_BUTTON_B: case AKEYCODE_BACK:        mask = (1 << 13); break; // Circle
            case AKEYCODE_BUTTON_X:                            mask = (1 << 15); break; // Square
            case AKEYCODE_BUTTON_Y:                            mask = (1 << 12); break; // Triangle
            case AKEYCODE_BUTTON_L1:                           mask = (1 << 10); break; // L1
            case AKEYCODE_BUTTON_R1:                           mask = (1 << 11); break; // R1
            case AKEYCODE_BUTTON_L2:                           mask = (1 << 8);  break; // L2
            case AKEYCODE_BUTTON_R2:                           mask = (1 << 9);  break; // R2
            case AKEYCODE_BUTTON_SELECT:                       mask = (1 << 0);  break; // Select
            case AKEYCODE_BUTTON_START: case AKEYCODE_MENU:    mask = (1 << 3);  break; // Start
            case AKEYCODE_DPAD_UP:                             mask = (1 << 4);  break; // Up
            case AKEYCODE_DPAD_RIGHT:                          mask = (1 << 5);  break; // Right
            case AKEYCODE_DPAD_DOWN:                           mask = (1 << 6);  break; // Down
            case AKEYCODE_DPAD_LEFT:                           mask = (1 << 7);  break; // Left
            default: break;
        }
        if (mask != 0) {
            if (down) g_pad_buttons &= ~mask;
            else g_pad_buttons |= mask;
            return 1;
        }
    } else if (type == AINPUT_EVENT_TYPE_MOTION) {
        float x = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_X, 0);
        float y = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Y, 0);
        float hat_x = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_HAT_X, 0);
        float hat_y = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_HAT_Y, 0);

        if (hat_x < -0.5f || x < -0.5f) g_pad_buttons &= ~(1 << 7); else g_pad_buttons |= (1 << 7);
        if (hat_x > 0.5f || x > 0.5f)  g_pad_buttons &= ~(1 << 5); else g_pad_buttons |= (1 << 5);
        if (hat_y < -0.5f || y < -0.5f) g_pad_buttons &= ~(1 << 4); else g_pad_buttons |= (1 << 4);
        if (hat_y > 0.5f || y > 0.5f)  g_pad_buttons &= ~(1 << 6); else g_pad_buttons |= (1 << 6);

        g_stick_x = static_cast<int16_t>(x * 32767.0f);
        g_stick_y = static_cast<int16_t>(y * 32767.0f);
        return 1;
    }
    return 0;
}

static void PrepareFileSystem(android_app* app) {
    const char* filesDir = app->activity ? app->activity->internalDataPath : nullptr;
    const char* extDir = app->activity ? app->activity->externalDataPath : nullptr;
    InitDiagLog(extDir, filesDir);

    if (!filesDir || chdir(filesDir) != 0) {
        LOGE("chdir to internalDataPath failed (%s)", filesDir ? filesDir : "<null>");
    } else {
        LOGI("chdir to internalDataPath succeeded: %s", filesDir);
    }
    mkdir("discimage", 0755);
    mkdir("userfiles", 0755);
    mkdir("saves", 0755);
}

static void PumpEventsOnce(android_app* app) {
    int events = 0;
    android_poll_source* source = nullptr;
    const int ident = ALooper_pollOnce(-1, nullptr, &events, reinterpret_cast<void**>(&source));
    if (ident >= 0 && source) {
        source->process(app, source);
    }
}

static void PumpEventsUntilWindowOrDestroy(android_app* app) {
    while (!app->destroyRequested && !g_window_ready) {
        PumpEventsOnce(app);
    }
}

extern "C" void android_main(struct android_app* state) {
    g_app = state;
    const char* filesDir = state->activity ? state->activity->internalDataPath : nullptr;
    const char* extDir = state->activity ? state->activity->externalDataPath : nullptr;
    InitDiagLog(extDir, filesDir);

    LOGI("=== Sheep Raider NativeActivity Startup ===");
    LOGI("internalDataPath: %s", filesDir ? filesDir : "<null>");
    LOGI("externalDataPath: %s", extDir ? extDir : "<null>");

    state->onAppCmd = HandleCommand;
    state->onInputEvent = HandleInput;

    PrepareFileSystem(state);

    // Wait synchronously for window before launching game thread
    LOGI("Waiting for APP_CMD_INIT_WINDOW before launching game thread...");
    PumpEventsUntilWindowOrDestroy(state);

    if (!state->destroyRequested && g_window) {
        LOGI("Window ready, launching game thread...");
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, 8 * 1024 * 1024);
        pthread_create(&g_game_thread, &attr, GameThreadFunc, nullptr);
        pthread_attr_destroy(&attr);
    }

    // Main thread pumps OS events until activity is destroyed
    while (!state->destroyRequested) {
        PumpEventsOnce(state);
    }

    LOGI("Main thread exiting, waiting for game thread...");
    g_running = false;
    if (g_game_thread != 0) {
        pthread_join(g_game_thread, nullptr);
    }
    LOGI("Sheep Raider terminated cleanly.");
}
