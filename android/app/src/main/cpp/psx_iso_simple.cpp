// psx_iso_simple.cpp — Standalone PS1 CD-ROM disc reader (BIN/CUE) for Android
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <string>
#include <android/log.h>

#define LOG_TAG "SheepRaiderISO"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

struct SimpleIsoHandle {
    FILE* file;
    uint32_t sector_count;
    bool is_raw_2352;
};

extern "C" {

void* iso_open(const char* path) {
    if (!path || !path[0]) {
        LOGE("iso_open: called with null or empty path");
        return nullptr;
    }
    LOGI("iso_open: attempting to open '%s'", path);
    
    // Check if path is a .cue file, if so try to find the .bin file next to it
    std::string filepath = path;
    if (filepath.length() > 4 && filepath.substr(filepath.length() - 4) == ".cue") {
        FILE* cue = fopen(path, "r");
        if (cue) {
            char line[256];
            while (fgets(line, sizeof(line), cue)) {
                char bin_name[256];
                if (sscanf(line, "FILE \"%[^\"]\"", bin_name) == 1 ||
                    sscanf(line, "FILE %s", bin_name) == 1) {
                    size_t slash = filepath.find_last_of("/\\");
                    if (slash != std::string::npos) {
                        filepath = filepath.substr(0, slash + 1) + bin_name;
                    } else {
                        filepath = bin_name;
                    }
                    LOGI("iso_open: parsed .cue, resolved bin to '%s'", filepath.c_str());
                    break;
                }
            }
            fclose(cue);
        } else {
            LOGE("iso_open: failed to read .cue file '%s'", path);
        }
    }

    FILE* f = fopen(filepath.c_str(), "rb");
    if (!f) {
        LOGI("iso_open: could not open '%s', trying original '%s'", filepath.c_str(), path);
        f = fopen(path, "rb");
        if (!f) {
            LOGE("iso_open: failed to open both '%s' and '%s'", filepath.c_str(), path);
            return nullptr;
        }
    }

    fseeko(f, 0, SEEK_END);
    int64_t size = ftello(f);
    fseeko(f, 0, SEEK_SET);

    if (size <= 0) {
        LOGE("iso_open: file '%s' has non-positive size (%lld)", filepath.c_str(), (long long)size);
        fclose(f);
        return nullptr;
    }

    SimpleIsoHandle* h = new SimpleIsoHandle();
    h->file = f;
    
    // PS1 images are typically 2352 bytes/sector (raw Mode 2 Form 1 or raw audio)
    // or 2048 bytes/sector (ISO Mode 1 / Mode 2)
    if (size % 2352 == 0) {
        h->is_raw_2352 = true;
        h->sector_count = (uint32_t)(size / 2352);
    } else {
        h->is_raw_2352 = false;
        h->sector_count = (uint32_t)(size / 2048);
    }

    LOGI("iso_open SUCCESS: opened '%s', size=%lld, is_raw_2352=%d, sectors=%u",
         filepath.c_str(), (long long)size, h->is_raw_2352 ? 1 : 0, h->sector_count);
    return h;
}

int iso_read_sector(void* handle, uint32_t lba, uint8_t* buffer, int size) {
    if (!handle || !buffer || size <= 0) return 0;
    SimpleIsoHandle* h = (SimpleIsoHandle*)handle;
    if (lba >= h->sector_count) return 0;

    int read_bytes = size > 2048 ? 2048 : size;
    int64_t offset = 0;
    if (h->is_raw_2352) {
        // In raw 2352 sectors, Mode 2 Form 1 user data begins at offset 24 (16 header + 8 subheader)
        offset = (int64_t)lba * 2352 + 24;
    } else {
        offset = (int64_t)lba * 2048;
    }

    if (fseeko(h->file, offset, SEEK_SET) != 0) return 0;
    size_t n = fread(buffer, 1, read_bytes, h->file);
    return n == (size_t)read_bytes ? 1 : 0;
}

int iso_read_raw_sector(void* handle, uint32_t lba, uint8_t* buffer, int size) {
    if (!handle || !buffer || size <= 0) return 0;
    SimpleIsoHandle* h = (SimpleIsoHandle*)handle;
    if (lba >= h->sector_count) return 0;

    int read_bytes = size > 2352 ? 2352 : size;
    int64_t offset = 0;
    if (h->is_raw_2352) {
        offset = (int64_t)lba * 2352;
    } else {
        offset = (int64_t)lba * 2048;
        read_bytes = size > 2048 ? 2048 : size;
    }

    if (fseeko(h->file, offset, SEEK_SET) != 0) return 0;
    size_t n = fread(buffer, 1, read_bytes, h->file);
    return n == (size_t)read_bytes ? 1 : 0;
}

int iso_read_subq(void*, uint32_t, uint8_t*, int, int* valid_out) {
    if (valid_out) *valid_out = 0;
    return 0;
}

int iso_has_subq_replacements(void*) {
    return 0;
}

uint32_t iso_sector_count(void* handle) {
    if (!handle) return 0;
    SimpleIsoHandle* h = (SimpleIsoHandle*)handle;
    return h->sector_count;
}

void iso_close(void* handle) {
    if (!handle) return;
    SimpleIsoHandle* h = (SimpleIsoHandle*)handle;
    if (h->file) fclose(h->file);
    delete h;
}

int iso_track_count(void*) {
    return 1;
}

uint32_t iso_track_start_lba(void*, int) {
    return 0;
}

uint32_t iso_track_pregap_lba(void*, int) {
    return 0;
}

int iso_track_is_audio(void*, int) {
    return 0;
}

} // extern "C"
