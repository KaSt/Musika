#ifndef MINIAUDIO_H
#define MINIAUDIO_H

// Minimal embedded subset inspired by miniaudio. Provides a small cross-platform
// audio device wrapper that opens the default playback device on Linux (ALSA via
// dynamic loading) and macOS (AudioQueue). Only the pieces needed by Musika are
// implemented.

#include <stdint.h>
#include <stddef.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dlfcn.h>

#if defined(__APPLE__)
#include <AudioToolbox/AudioQueue.h>
#include <AudioToolbox/AudioFormat.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t ma_result;
typedef uint8_t ma_uint8;
typedef uint16_t ma_uint16;
typedef uint32_t ma_uint32;
typedef uint64_t ma_uint64;
typedef int32_t ma_int32;

typedef struct ma_context {
    int placeholder;
} ma_context;

struct ma_device;
typedef void (*ma_device_callback_proc)(struct ma_device *pDevice, void *pOutput, const void *pInput, ma_uint32 frameCount);

typedef struct ma_device_config {
    ma_uint32 sampleRate;
    ma_uint32 periodSizeInFrames;
    ma_uint32 channels;
    ma_device_callback_proc dataCallback;
    void *pUserData;
} ma_device_config;

static inline ma_device_config ma_device_config_init(ma_uint32 channels, ma_uint32 sampleRate) {
    ma_device_config cfg;
    cfg.sampleRate = sampleRate ? sampleRate : 48000;
    cfg.periodSizeInFrames = 512;
    cfg.channels = channels ? channels : 2;
    cfg.dataCallback = NULL;
    cfg.pUserData = NULL;
    return cfg;
}

static inline ma_result ma_context_init(void *pCallbacks, ma_uint32 flags, ma_context *pContext) {
    (void)pCallbacks;
    (void)flags;
    (void)pContext;
    return 0;
}

static inline void ma_context_uninit(ma_context *pContext) {
    (void)pContext;
}

// --- Linux ALSA backend via dynamic loading ---
#if defined(__linux__)
#include <sound/asound.h>

typedef struct _snd_pcm snd_pcm_t;
typedef long snd_pcm_sframes_t;
typedef unsigned long snd_pcm_uframes_t;

typedef struct ma__alsa_api {
    void *handle;
    int (*open)(snd_pcm_t **, const char *, int, int);
    int (*close)(snd_pcm_t *);
    int (*prepare)(snd_pcm_t *);
    snd_pcm_sframes_t (*writei)(snd_pcm_t *, const void *, snd_pcm_uframes_t);
    int (*set_params)(snd_pcm_t *, snd_pcm_format_t, snd_pcm_access_t, unsigned int, unsigned int, int, unsigned int);
} ma__alsa_api;
#endif

#if defined(__APPLE__)
typedef struct ma__aq_state {
    AudioQueueRef queue;
    AudioQueueBufferRef buffers[3];
    UInt32 framesPerBuffer;
} ma__aq_state;
#endif

typedef struct ma_device {
    ma_device_config config;
    int running;
    pthread_t thread;
#if defined(__linux__)
    ma__alsa_api alsa;
    snd_pcm_t *pcm;
    float *mix_buffer;
#elif defined(__APPLE__)
    ma__aq_state aq;
    void *userData;
#else
    float *mix_buffer;
#endif
    void *pUserData;
} ma_device;

#if defined(__linux__)
static int ma__load_alsa(ma__alsa_api *api) {
    if (!api) return -1;
    memset(api, 0, sizeof(*api));
    api->handle = dlopen("libasound.so.2", RTLD_LAZY);
    if (!api->handle) return -1;
    *(void **)(&api->open) = dlsym(api->handle, "snd_pcm_open");
    *(void **)(&api->close) = dlsym(api->handle, "snd_pcm_close");
    *(void **)(&api->prepare) = dlsym(api->handle, "snd_pcm_prepare");
    *(void **)(&api->writei) = dlsym(api->handle, "snd_pcm_writei");
    *(void **)(&api->set_params) = dlsym(api->handle, "snd_pcm_set_params");
    if (!api->open || !api->close || !api->prepare || !api->writei || !api->set_params) {
        dlclose(api->handle);
        memset(api, 0, sizeof(*api));
        return -1;
    }
    return 0;
}

static void ma__unload_alsa(ma__alsa_api *api) {
    if (!api) return;
    if (api->handle) dlclose(api->handle);
    memset(api, 0, sizeof(*api));
}

static void *ma__device_thread_alsa(void *user) {
    ma_device *dev = (ma_device *)user;
    const ma_uint32 frames = dev->config.periodSizeInFrames;
    const ma_uint32 channels = dev->config.channels;
    const size_t sample_count = (size_t)frames * channels;
    while (dev->running) {
        memset(dev->mix_buffer, 0, sample_count * sizeof(float));
        if (dev->config.dataCallback) {
            dev->config.dataCallback(dev, dev->mix_buffer, NULL, frames);
        }
        snd_pcm_sframes_t written = dev->alsa.writei(dev->pcm, dev->mix_buffer, frames);
        if (written < 0) {
            dev->alsa.prepare(dev->pcm);
        }
    }
    return NULL;
}
#endif

#if defined(__APPLE__)
static void ma__aq_callback(void *userData, AudioQueueRef inAQ, AudioQueueBufferRef inBuffer) {
    ma_device *dev = (ma_device *)userData;
    if (!dev || !dev->config.dataCallback) return;
    float *out = (float *)inBuffer->mAudioData;
    const ma_uint32 frames = dev->config.periodSizeInFrames;
    const ma_uint32 channels = dev->config.channels;
    memset(out, 0, frames * channels * sizeof(float));
    dev->config.dataCallback(dev, out, NULL, frames);
    inBuffer->mAudioDataByteSize = frames * channels * sizeof(float);
    AudioQueueEnqueueBuffer(inAQ, inBuffer, 0, NULL);
}
#endif

static inline ma_result ma_device_init(ma_context *pContext, const ma_device_config *pConfig, ma_device *pDevice) {
    (void)pContext;
    if (!pConfig || !pDevice) return -1;
    memset(pDevice, 0, sizeof(*pDevice));
    pDevice->config = *pConfig;
    pDevice->pUserData = pConfig->pUserData;

#if defined(__linux__)
    if (ma__load_alsa(&pDevice->alsa) != 0) return -1;
    if (pDevice->alsa.open(&pDevice->pcm, "default", SNDRV_PCM_STREAM_PLAYBACK, 0) != 0) {
        ma__unload_alsa(&pDevice->alsa);
        return -1;
    }
    if (pDevice->alsa.set_params(pDevice->pcm, SNDRV_PCM_FORMAT_FLOAT_LE, SNDRV_PCM_ACCESS_RW_INTERLEAVED,
                                 pDevice->config.channels, pDevice->config.sampleRate, 1, 500000) != 0) {
        pDevice->alsa.close(pDevice->pcm);
        ma__unload_alsa(&pDevice->alsa);
        return -1;
    }
    size_t sample_count = (size_t)pDevice->config.periodSizeInFrames * pDevice->config.channels;
    pDevice->mix_buffer = (float *)malloc(sample_count * sizeof(float));
    if (!pDevice->mix_buffer) {
        pDevice->alsa.close(pDevice->pcm);
        ma__unload_alsa(&pDevice->alsa);
        return -1;
    }
#elif defined(__APPLE__)
    AudioStreamBasicDescription fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.mSampleRate = pDevice->config.sampleRate;
    fmt.mFormatID = kAudioFormatLinearPCM;
    fmt.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    fmt.mBitsPerChannel = 32;
    fmt.mChannelsPerFrame = pDevice->config.channels;
    fmt.mBytesPerFrame = fmt.mChannelsPerFrame * sizeof(float);
    fmt.mFramesPerPacket = 1;
    fmt.mBytesPerPacket = fmt.mBytesPerFrame * fmt.mFramesPerPacket;

    pDevice->aq.framesPerBuffer = pDevice->config.periodSizeInFrames;
    OSStatus st = AudioQueueNewOutput(&fmt, ma__aq_callback, pDevice, NULL, NULL, 0, &pDevice->aq.queue);
    if (st != 0) return -1;
    for (int i = 0; i < 3; ++i) {
        st = AudioQueueAllocateBuffer(pDevice->aq.queue, fmt.mBytesPerFrame * pDevice->aq.framesPerBuffer, &pDevice->aq.buffers[i]);
        if (st != 0) return -1;
        memset(pDevice->aq.buffers[i]->mAudioData, 0, pDevice->aq.buffers[i]->mAudioDataBytesCapacity);
        pDevice->aq.buffers[i]->mAudioDataByteSize = fmt.mBytesPerFrame * pDevice->aq.framesPerBuffer;
        AudioQueueEnqueueBuffer(pDevice->aq.queue, pDevice->aq.buffers[i], 0, NULL);
    }
#else
    size_t sample_count = (size_t)pDevice->config.periodSizeInFrames * pDevice->config.channels;
    pDevice->mix_buffer = (float *)malloc(sample_count * sizeof(float));
    if (!pDevice->mix_buffer) return -1;
#endif
    return 0;
}

static inline ma_result ma_device_start(ma_device *pDevice) {
    if (!pDevice) return -1;
#if defined(__linux__)
    pDevice->running = 1;
    if (pthread_create(&pDevice->thread, NULL, ma__device_thread_alsa, pDevice) != 0) {
        pDevice->running = 0;
        return -1;
    }
    return 0;
#elif defined(__APPLE__)
    return AudioQueueStart(pDevice->aq.queue, NULL);
#else
    pDevice->running = 1;
    return 0;
#endif
}

static inline void ma_device_stop(ma_device *pDevice) {
    if (!pDevice) return;
#if defined(__linux__)
    if (pDevice->running) {
        pDevice->running = 0;
        pthread_join(pDevice->thread, NULL);
    }
#elif defined(__APPLE__)
    AudioQueueStop(pDevice->aq.queue, true);
#else
    pDevice->running = 0;
#endif
}

static inline void ma_device_uninit(ma_device *pDevice) {
    if (!pDevice) return;
#if defined(__linux__)
    ma_device_stop(pDevice);
    if (pDevice->pcm) pDevice->alsa.close(pDevice->pcm);
    free(pDevice->mix_buffer);
    ma__unload_alsa(&pDevice->alsa);
#elif defined(__APPLE__)
    ma_device_stop(pDevice);
    AudioQueueDispose(pDevice->aq.queue, true);
#else
    free(pDevice->mix_buffer);
#endif
}

#ifdef __cplusplus
}
#endif

#endif // MINIAUDIO_H
