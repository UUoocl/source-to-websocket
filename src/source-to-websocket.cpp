#include <obs-module.h>
#include <obs.h>
#include <vector>
#include <atomic>
#include <string>
#include <mutex>
#include <math.h>
#include <algorithm>

#include <util/platform.h>
#include <turbojpeg.h>

#ifdef __APPLE__
#include <Accelerate/Accelerate.h>
#endif

OBS_DECLARE_MODULE()
OBS_MODULE_AUTHOR("Antigravity")
OBS_MODULE_USE_DEFAULT_LOCALE("source-to-websocket", "en-US")

// Optimized Base64 encoder
static const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64_encode(const unsigned char* bytes_to_encode, size_t in_len) {
    std::string ret;
    ret.reserve(((in_len + 2) / 3) * 4);
    int i = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];

    while (in_len--) {
        char_array_3[i++] = *(bytes_to_encode++);
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;
            for (i = 0; (i < 4); i++) ret += base64_chars[char_array_4[i]];
            i = 0;
        }
    }
    if (i) {
        for (int j = i; j < 3; j++) char_array_3[j] = '\0';
        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        char_array_4[3] = char_array_3[2] & 0x3f;
        for (int j = 0; (j < i + 1); j++) ret += base64_chars[char_array_4[j]];
        while ((i++ < 3)) ret += '=';
    }
    return ret;
}

struct TaskState {
    std::string type = "frame_source";
	std::atomic<uint64_t> lastFrameTime{0};
    std::atomic<int> targetWidth{256};
    std::atomic<int> fps{15};
};

// Thread-safe TurboJPEG handle management
static tjhandle get_tj_handle() {
    static thread_local tjhandle handle = nullptr;
    if (!handle) handle = tjInitCompress();
    return handle;
}

#ifdef __APPLE__
static void scale_plane_vimage(const uint8_t *src, int srcW, int srcH, int srcStep,
                               uint8_t *dst, int dstW, int dstH, int dstStep) {
    vImage_Buffer srcBuf = {(void*)src, (size_t)srcH, (size_t)srcW, (size_t)srcStep};
    vImage_Buffer dstBuf = {(void*)dst, (size_t)dstH, (size_t)dstW, (size_t)dstStep};
    vImageScale_Planar8(&srcBuf, &dstBuf, NULL, kvImageHighQualityResampling);
}
#endif

static void transmit_frame(const char* type, struct obs_source_frame *frame, int targetWidth) {
    if (!frame) return;
    tjhandle tj_handle = get_tj_handle();
    if (!tj_handle) return;

    unsigned char *jpegBuf = nullptr;
    unsigned long jpegSize = 0;
    int width = (int)frame->width;
    int height = (int)frame->height;
    int outWidth = width;
    int outHeight = height;

    if (width > targetWidth) {
        double scale = (double)targetWidth / width;
        outWidth = targetWidth;
        outHeight = (int)(height * scale);
        outWidth &= ~1; outHeight &= ~1;
    }

    if (frame->format == VIDEO_FORMAT_I420 || frame->format == VIDEO_FORMAT_NV12) {
        // ... (existing I420/NV12 logic)
        std::vector<uint8_t> dstYData, dstUData, dstVData;
        const unsigned char *planes[3];
        int strides[3];
        bool is_nv12 = (frame->format == VIDEO_FORMAT_NV12);
        if (width > targetWidth) {
            dstYData.resize(outWidth * outHeight);
            dstUData.resize((outWidth / 2) * (outHeight / 2));
            dstVData.resize((outWidth / 2) * (outHeight / 2));
#ifdef __APPLE__
            scale_plane_vimage(frame->data[0], width, height, (int)frame->linesize[0], dstYData.data(), outWidth, outHeight, outWidth);
            if (is_nv12) {
                memset(dstUData.data(), 128, dstUData.size());
                memset(dstVData.data(), 128, dstVData.size());
            } else {
                scale_plane_vimage(frame->data[1], width / 2, height / 2, (int)frame->linesize[1], dstUData.data(), outWidth / 2, outHeight / 2, outWidth / 2);
                scale_plane_vimage(frame->data[2], width / 2, height / 2, (int)frame->linesize[2], dstVData.data(), outWidth / 2, outHeight / 2, outWidth / 2);
            }
#endif
            planes[0] = dstYData.data(); planes[1] = dstUData.data(); planes[2] = dstVData.data();
            strides[0] = outWidth; strides[1] = outWidth / 2; strides[2] = outWidth / 2;
        } else {
            planes[0] = frame->data[0]; 
            if (is_nv12) {
                dstUData.assign((outWidth / 2) * (outHeight / 2), 128);
                dstVData.assign((outWidth / 2) * (outHeight / 2), 128);
                planes[1] = dstUData.data(); planes[2] = dstVData.data();
                strides[0] = (int)frame->linesize[0]; strides[1] = outWidth / 2; strides[2] = outWidth / 2;
            } else {
                planes[1] = frame->data[1]; planes[2] = frame->data[2];
                strides[0] = (int)frame->linesize[0]; strides[1] = (int)frame->linesize[1]; strides[2] = (int)frame->linesize[2];
            }
        }
        tjCompressFromYUVPlanes(tj_handle, planes, outWidth, strides, outHeight, TJSAMP_420, &jpegBuf, &jpegSize, 70, TJFLAG_FASTDCT);
    } else if (frame->format == VIDEO_FORMAT_BGRA || frame->format == VIDEO_FORMAT_BGRX || frame->format == VIDEO_FORMAT_UYVY) {
        std::vector<uint8_t> dstData;
        std::vector<uint8_t> tmpBgra;
        const unsigned char* srcPtr = frame->data[0];
        int srcStride = (int)frame->linesize[0];

        if (frame->format == VIDEO_FORMAT_UYVY) {
            tmpBgra.resize(width * height * 4);
            const uint8_t* src = frame->data[0];
            uint8_t* dst = tmpBgra.data();
            for (int i = 0; i < width * height / 2; ++i) {
                int u = src[i * 4 + 0] - 128;
                int y0 = src[i * 4 + 1];
                int v = src[i * 4 + 2] - 128;
                int y1 = src[i * 4 + 3];

                int r_u = (116130 * v) >> 16;
                int g_u = (22554 * u + 46802 * v) >> 16;
                int b_u = (147015 * u) >> 16;

                dst[i * 8 + 0] = std::clamp(y0 + b_u, 0, 255); // B
                dst[i * 8 + 1] = std::clamp(y0 - g_u, 0, 255); // G
                dst[i * 8 + 2] = std::clamp(y0 + r_u, 0, 255); // R
                dst[i * 8 + 3] = 255;                          // A
                
                dst[i * 8 + 4] = std::clamp(y1 + b_u, 0, 255); // B
                dst[i * 8 + 5] = std::clamp(y1 - g_u, 0, 255); // G
                dst[i * 8 + 6] = std::clamp(y1 + r_u, 0, 255); // R
                dst[i * 8 + 7] = 255;                          // A
            }
            srcPtr = tmpBgra.data();
            srcStride = width * 4;
        }

        if (width > targetWidth) {
            dstData.resize(outWidth * outHeight * 4);
#ifdef __APPLE__
            vImage_Buffer sBuf = {(void*)srcPtr, (size_t)height, (size_t)width, (size_t)srcStride};
            vImage_Buffer dBuf = {(void*)dstData.data(), (size_t)outHeight, (size_t)outWidth, (size_t)outWidth * 4};
            vImageScale_ARGB8888(&sBuf, &dBuf, NULL, kvImageHighQualityResampling);
#endif
            srcPtr = dstData.data(); srcStride = outWidth * 4;
        }
        tjCompress2(tj_handle, srcPtr, outWidth, srcStride, outHeight, TJPF_BGRA, &jpegBuf, &jpegSize, TJSAMP_420, 70, TJFLAG_FASTDCT);
    } else { 
        static bool warned = false;
        if (!warned) {
            blog(LOG_WARNING, "[Source to WebSocket] Unsupported frame format: %d", frame->format);
            warned = true;
        }
        return; 
    }

    if (jpegBuf && jpegSize > 0) {
        std::string b64 = base64_encode((const unsigned char*)jpegBuf, jpegSize);
        
        calldata_t cd;
        calldata_init(&cd);
        calldata_set_string(&cd, "data", b64.c_str());
        calldata_set_string(&cd, "topic", type);

        signal_handler_t *sh = obs_get_signal_handler();
        if (sh) {
            signal_handler_signal(sh, "media_warp_transmit_topic", &cd);
        }
        calldata_free(&cd);

        tjFree(jpegBuf);
    }
}

static void frame_to_websocket_update(void *data, obs_data_t *settings) {
    TaskState *state = (TaskState *)data;
    state->type = obs_data_get_string(settings, "type");
    state->fps.store((int)obs_data_get_int(settings, "fps"), std::memory_order_relaxed);
    state->targetWidth.store((int)obs_data_get_int(settings, "target_width"), std::memory_order_relaxed);
    blog(LOG_INFO, "[Source to WebSocket] Filter updated: type=%s, fps=%d, width=%d", 
        state->type.c_str(), state->fps.load(), state->targetWidth.load());
}

static void *frame_to_websocket_create(obs_data_t *settings, obs_source_t *source) {
    (void)source;
    blog(LOG_INFO, "[Source to WebSocket] Filter instance created");
    TaskState *state = new TaskState();
    frame_to_websocket_update(state, settings);
    return state;
}

static void frame_to_websocket_destroy(void *data) {
    blog(LOG_INFO, "[Source to WebSocket] Filter instance destroyed");
    delete (TaskState *)data;
}

static struct obs_source_frame *frame_to_websocket_video(void *data, struct obs_source_frame *frame) {
    TaskState *state = (TaskState *)data;
    
    static bool first_frame = true;
    if (first_frame) {
        blog(LOG_INFO, "[Source to WebSocket] First frame received! Format: %d, Res: %dx%d", 
            frame->format, frame->width, frame->height);
        first_frame = false;
    }

    uint64_t now = os_gettime_ns();
    int current_fps = state->fps.load(std::memory_order_relaxed);
    if (current_fps <= 0) current_fps = 15;
    uint64_t interval = 1000000000ULL / current_fps;
    uint64_t last = state->lastFrameTime.load(std::memory_order_relaxed);
    
    if (now - last < interval) return frame;
    
    state->lastFrameTime.store(now, std::memory_order_relaxed);

    transmit_frame(state->type.c_str(), frame, state->targetWidth.load(std::memory_order_relaxed));
    return frame;
}

static const char *frame_to_websocket_get_name(void *unused) {
    (void)unused;
    return "frame to websocket";
}

static obs_properties_t *frame_to_websocket_properties(void *data) {
    (void)data;
    obs_properties_t *props = obs_properties_create();
    obs_properties_add_text(props, "type", "WebSocket Type", OBS_TEXT_DEFAULT);
    
    obs_properties_add_int(props, "fps", "Target FPS", 1, 60, 1);
    obs_properties_add_int(props, "target_width", "Scale Width (px)", 64, 1024, 8);
    
    return props;
}

static void frame_to_websocket_defaults(obs_data_t *settings) {
    obs_data_set_default_string(settings, "type", "frame_source");
    obs_data_set_default_int(settings, "fps", 15);
    obs_data_set_default_int(settings, "target_width", 256);
}

struct TextFilterState {
    obs_source_t *context;
    std::string last_text;
    std::string setting_key;
    std::string topic;
};

static void text_to_websocket_update(void *data, obs_data_t *settings) {
    TextFilterState *state = (TextFilterState *)data;
    state->setting_key = obs_data_get_string(settings, "setting_key");
    state->topic = obs_data_get_string(settings, "topic");
}

static void *text_to_websocket_create(obs_data_t *settings, obs_source_t *source) {
    TextFilterState *state = new TextFilterState();
    state->context = source;
    text_to_websocket_update(state, settings);
    return state;
}

static void text_to_websocket_destroy(void *data) {
    delete (TextFilterState *)data;
}

static void text_to_websocket_tick(void *data, float seconds) {
    UNUSED_PARAMETER(seconds);
    TextFilterState *state = (TextFilterState *)data;
    obs_source_t *parent = obs_filter_get_parent(state->context);
    if (!parent) return;

    obs_data_t *parent_settings = obs_source_get_settings(parent);
    const char *current_text_raw = obs_data_get_string(parent_settings, state->setting_key.c_str());
    std::string current_text = current_text_raw ? current_text_raw : "";
    obs_data_release(parent_settings);

    if (current_text != state->last_text) {
        state->last_text = current_text;
        
        obs_data_t *packet = obs_data_create();
        obs_data_set_string(packet, "t", state->topic.c_str());
        obs_data_set_string(packet, "v", current_text.c_str());
        obs_data_set_string(packet, "a", state->topic.c_str());
        
        calldata_t cd;
        calldata_init(&cd);
        calldata_set_ptr(&cd, "packet", packet);
        
        signal_handler_t *sh = obs_get_signal_handler();
        if (sh) {
            signal_handler_signal(sh, "media_warp_transmit", &cd);
        }
        
        calldata_free(&cd);
        obs_data_release(packet);
    }
}

static const char *text_to_websocket_get_name(void *unused) {
    (void)unused;
    return "text to websocket";
}

static obs_properties_t *text_to_websocket_properties(void *data) {
    (void)data;
    obs_properties_t *props = obs_properties_create();
    obs_properties_add_text(props, "setting_key", "Setting Key", OBS_TEXT_DEFAULT);
    obs_properties_add_text(props, "topic", "WebSocket Topic", OBS_TEXT_DEFAULT);
    return props;
}

static void text_to_websocket_defaults(obs_data_t *settings) {
    obs_data_set_default_string(settings, "setting_key", "text");
    obs_data_set_default_string(settings, "topic", "text_source");
}

// --- Audio FFT to WebSocket Filter ---

struct AudioFFTState {
    std::string topic = "audio_fft";
    std::atomic<int> fps{30};
    std::atomic<int> num_bands{32};
    std::atomic<float> smoothing{0.5f};
    std::atomic<float> min_db{-60.0f};
    std::atomic<float> max_db{0.0f};

    uint64_t last_frame_time = 0;
    
    // vDSP / FFT state
    int log2n = 11; // 2048 samples
    int window_size = 2048;
#ifdef __APPLE__
    FFTSetup fft_setup = nullptr;
#endif
    
    std::vector<float> sample_buffer;
    std::vector<float> window;
    
    std::vector<float> smoothed_bands;
    
    std::mutex audio_mutex;

    AudioFFTState() {
#ifdef __APPLE__
        fft_setup = vDSP_create_fftsetup(log2n, FFT_RADIX2);
        window.resize(window_size);
        vDSP_hann_window(window.data(), window_size, vDSP_HANN_NORM);
#endif
    }
    
    ~AudioFFTState() {
#ifdef __APPLE__
        if (fft_setup) {
            vDSP_destroy_fftsetup(fft_setup);
        }
#endif
    }
};

static void audio_fft_to_websocket_update(void *data, obs_data_t *settings) {
    AudioFFTState *state = (AudioFFTState *)data;
    std::lock_guard<std::mutex> lock(state->audio_mutex);
    state->topic = obs_data_get_string(settings, "topic");
    state->fps = (int)obs_data_get_int(settings, "fps");
    
    int new_bands = (int)obs_data_get_int(settings, "num_bands");
    if (new_bands != state->num_bands) {
        state->num_bands = new_bands;
        state->smoothed_bands.clear();
        state->smoothed_bands.resize(new_bands, state->min_db);
    }
    
    state->smoothing = (float)obs_data_get_double(settings, "smoothing");
    state->min_db = (float)obs_data_get_double(settings, "min_db");
    state->max_db = (float)obs_data_get_double(settings, "max_db");
}

static void *audio_fft_to_websocket_create(obs_data_t *settings, obs_source_t *source) {
    (void)source;
    AudioFFTState *state = new AudioFFTState();
    audio_fft_to_websocket_update(state, settings);
    return state;
}

static void audio_fft_to_websocket_destroy(void *data) {
    delete (AudioFFTState *)data;
}

static struct obs_audio_data *audio_fft_to_websocket_audio(void *data, struct obs_audio_data *audio) {
    AudioFFTState *state = (AudioFFTState *)data;
    
#ifdef __APPLE__
    uint64_t now = os_gettime_ns();
    int current_fps = state->fps.load(std::memory_order_relaxed);
    if (current_fps <= 0) current_fps = 30;
    uint64_t interval = 1000000000ULL / current_fps;

    std::lock_guard<std::mutex> lock(state->audio_mutex);

    uint32_t frames = audio->frames;
    float* src = (float*)audio->data[0]; // Channel 0
    if (!src) return audio;

    // Append to buffer
    for (uint32_t i = 0; i < frames; ++i) {
        state->sample_buffer.push_back(src[i]);
    }

    if (now - state->last_frame_time >= interval && state->sample_buffer.size() >= state->window_size) {
        state->last_frame_time = now;
        
        int start_idx = state->sample_buffer.size() - state->window_size;
        
        int half_size = state->window_size / 2;
        std::vector<float> real(half_size, 0.0f);
        std::vector<float> imag(half_size, 0.0f);
        DSPSplitComplex splitComplex = { real.data(), imag.data() };
        
        std::vector<float> windowed(state->window_size, 0.0f);
        vDSP_vmul(state->sample_buffer.data() + start_idx, 1, state->window.data(), 1, windowed.data(), 1, state->window_size);
        vDSP_ctoz((DSPComplex*)windowed.data(), 2, &splitComplex, 1, half_size);
        
        vDSP_fft_zrip(state->fft_setup, &splitComplex, 1, state->log2n, FFT_FORWARD);
        
        std::vector<float> magnitudes(half_size, 0.0f);
        vDSP_zvabs(&splitComplex, 1, magnitudes.data(), 1, half_size);
        
        float scale = 1.0f / (2.0f * state->window_size);
        vDSP_vsmul(magnitudes.data(), 1, &scale, magnitudes.data(), 1, half_size);
        
        float ref = 1.0f;
        std::vector<float> db(half_size, 0.0f);
        vDSP_vdbcon(magnitudes.data(), 1, &ref, db.data(), 1, half_size, 1);
        
        int num_bands = state->num_bands.load();
        float smoothing = state->smoothing.load();
        float min_db = state->min_db.load();
        float max_db = state->max_db.load();
        
        if (state->smoothed_bands.size() != num_bands) {
            state->smoothed_bands.resize(num_bands, min_db);
        }
        
        std::vector<uint8_t> payload(num_bands, 0);
        
        int usable_bins = half_size / 2; 
        float bins_per_band = (float)usable_bins / num_bands;
        
        for (int i = 0; i < num_bands; ++i) {
            int start_bin = 1 + (int)(i * bins_per_band);
            int end_bin = 1 + (int)((i + 1) * bins_per_band);
            if (end_bin > half_size) end_bin = half_size;
            
            float max_val = -1000.0f;
            for (int j = start_bin; j < end_bin; ++j) {
                if (db[j] > max_val) max_val = db[j];
            }
            
            float current = state->smoothed_bands[i];
            float smoothed = current * smoothing + max_val * (1.0f - smoothing);
            state->smoothed_bands[i] = smoothed;
            
            float normalized = (smoothed - min_db) / (max_db - min_db);
            normalized = std::clamp(normalized, 0.0f, 1.0f);
            payload[i] = (uint8_t)(normalized * 255.0f);
        }
        
        std::string b64 = base64_encode(payload.data(), payload.size());
        
        calldata_t cd;
        calldata_init(&cd);
        calldata_set_string(&cd, "data", b64.c_str());
        calldata_set_string(&cd, "topic", state->topic.c_str());

        signal_handler_t *sh = obs_get_signal_handler();
        if (sh) {
            signal_handler_signal(sh, "media_warp_transmit_topic", &cd);
        }
        calldata_free(&cd);
        
        state->sample_buffer.clear();
    } else if (state->sample_buffer.size() > state->window_size * 2) {
        int drop = state->sample_buffer.size() - state->window_size;
        state->sample_buffer.erase(state->sample_buffer.begin(), state->sample_buffer.begin() + drop);
    }
#endif
    return audio;
}

static const char *audio_fft_to_websocket_get_name(void *unused) {
    (void)unused;
    return "audio fft to websocket";
}

static obs_properties_t *audio_fft_to_websocket_properties(void *data) {
    (void)data;
    obs_properties_t *props = obs_properties_create();
    obs_properties_add_text(props, "topic", "WebSocket Topic", OBS_TEXT_DEFAULT);
    obs_properties_add_int(props, "fps", "Target FPS", 1, 120, 1);
    obs_properties_add_int(props, "num_bands", "Number of Bands", 4, 256, 4);
    obs_properties_add_float_slider(props, "smoothing", "Smoothing Factor", 0.0, 0.99, 0.01);
    obs_properties_add_float(props, "min_db", "Min dB (Floor)", -120.0, 0.0, 1.0);
    obs_properties_add_float(props, "max_db", "Max dB (Ceiling)", -60.0, 20.0, 1.0);
    return props;
}

static void audio_fft_to_websocket_defaults(obs_data_t *settings) {
    obs_data_set_default_string(settings, "topic", "audio_fft");
    obs_data_set_default_int(settings, "fps", 30);
    obs_data_set_default_int(settings, "num_bands", 32);
    obs_data_set_default_double(settings, "smoothing", 0.5);
    obs_data_set_default_double(settings, "min_db", -60.0);
    obs_data_set_default_double(settings, "max_db", 0.0);
}

bool obs_module_load(void) {
    // Register Frame filter
    obs_source_info frame_info = {};
    frame_info.id = "frame_to_websocket";
    frame_info.type = OBS_SOURCE_TYPE_FILTER;
    frame_info.output_flags = OBS_SOURCE_VIDEO;
    frame_info.get_name = frame_to_websocket_get_name;
    frame_info.create = frame_to_websocket_create;
    frame_info.destroy = frame_to_websocket_destroy;
    frame_info.update = frame_to_websocket_update;
    frame_info.get_properties = frame_to_websocket_properties;
    frame_info.get_defaults = frame_to_websocket_defaults;
    frame_info.filter_video = frame_to_websocket_video;

    obs_register_source(&frame_info);

    // Register Text filter
    obs_source_info text_info = {};
    text_info.id = "text_to_websocket";
    text_info.type = OBS_SOURCE_TYPE_FILTER;
    text_info.output_flags = OBS_SOURCE_VIDEO; // Filters are usually video filters to get ticks
    text_info.get_name = text_to_websocket_get_name;
    text_info.create = text_to_websocket_create;
    text_info.destroy = text_to_websocket_destroy;
    text_info.update = text_to_websocket_update;
    text_info.get_properties = text_to_websocket_properties;
    text_info.get_defaults = text_to_websocket_defaults;
    text_info.video_tick = text_to_websocket_tick;

    obs_register_source(&text_info);
    
    // Register Audio FFT filter
    obs_source_info fft_info = {};
    fft_info.id = "audio_fft_to_websocket";
    fft_info.type = OBS_SOURCE_TYPE_FILTER;
    fft_info.output_flags = OBS_SOURCE_AUDIO;
    fft_info.get_name = audio_fft_to_websocket_get_name;
    fft_info.create = audio_fft_to_websocket_create;
    fft_info.destroy = audio_fft_to_websocket_destroy;
    fft_info.update = audio_fft_to_websocket_update;
    fft_info.get_properties = audio_fft_to_websocket_properties;
    fft_info.get_defaults = audio_fft_to_websocket_defaults;
    fft_info.filter_audio = audio_fft_to_websocket_audio;

    obs_register_source(&fft_info);
    
    blog(LOG_INFO, "[Source to WebSocket] Filters registered");
    return true;
}

void obs_module_unload(void) {}
