
#include "config.h"

#include "context.h"

#include <stdexcept>
#include <algorithm>
#include <functional>
#include <memory>
#include <iostream>
#include <sstream>
#include <fstream>
#include <cstring>
#include <map>
#include <new>

#include "alc.h"

#ifdef HAVE_VORBISFILE
#include "decoders/vorbisfile.hpp"
#endif
#ifdef HAVE_LIBFLAC
#include "decoders/flac.hpp"
#endif
#ifdef HAVE_OPUSFILE
#include "decoders/opusfile.hpp"
#endif
#ifdef HAVE_LIBSNDFILE
#include "decoders/sndfile.hpp"
#endif
#ifdef HAVE_MPG123
#include "decoders/mpg123.hpp"
#endif
#include "decoders/wave.hpp"

#include "devicemanager.h"
#include "device.h"
#include "buffer.h"
#include "source.h"
#include "auxeffectslot.h"
#include "effect.h"
#include <sourcegroup.h>

namespace alure
{

static const std::pair<String,UniquePtr<DecoderFactory>> sDefaultDecoders[] = {
    { "_alure_int_wave", MakeUnique<WaveDecoderFactory>() },

#ifdef HAVE_VORBISFILE
    { "_alure_int_vorbis", MakeUnique<VorbisFileDecoderFactory>() },
#endif
#ifdef HAVE_LIBFLAC
    { "_alure_int_flac", MakeUnique<FlacDecoderFactory>() },
#endif
#ifdef HAVE_OPUSFILE
    { "_alure_int_opus", MakeUnique<OpusFileDecoderFactory>() },
#endif
#ifdef HAVE_LIBSNDFILE
    { "_alure_int_sndfile", MakeUnique<SndFileDecoderFactory>() },
#endif
#ifdef HAVE_MPG123
    { "_alure_int_mpg123", MakeUnique<Mpg123DecoderFactory>() },
#endif
};

static std::map<String,UniquePtr<DecoderFactory>> sDecoders;


template<typename T>
static SharedPtr<Decoder> GetDecoder(const String &name, UniquePtr<std::istream> &file, T start, T end)
{
    while(start != end)
    {
        DecoderFactory *factory = start->second.get();
        auto decoder = factory->createDecoder(file);
        if(decoder) return decoder;

        if(!file || !(file->clear(),file->seekg(0)))
            throw std::runtime_error("Failed to rewind "+name+" for the next decoder factory");

        ++start;
    }

    return nullptr;
}

static SharedPtr<Decoder> GetDecoder(const String &name, UniquePtr<std::istream> file)
{
    auto decoder = GetDecoder(name, file, sDecoders.begin(), sDecoders.end());
    if(!decoder) decoder = GetDecoder(name, file, std::begin(sDefaultDecoders), std::end(sDefaultDecoders));
    if(!decoder) throw std::runtime_error("No decoder for "+name);
    return decoder;
}

void RegisterDecoder(const String &name, UniquePtr<DecoderFactory> factory)
{
    while(sDecoders.find(name) != sDecoders.end())
        throw std::runtime_error("Decoder factory \""+name+"\" already registered");
    sDecoders.insert(std::make_pair(name, std::move(factory)));
}

UniquePtr<DecoderFactory> UnregisterDecoder(const String &name)
{
    auto iter = sDecoders.find(name);
    if(iter != sDecoders.end())
    {
        UniquePtr<DecoderFactory> factory = std::move(iter->second);
        sDecoders.erase(iter);
        return factory;
    }
    return nullptr;
}


class DefaultFileIOFactory : public FileIOFactory {
    UniquePtr<std::istream> openFile(const String &name) override final
    {
        auto file = MakeUnique<std::ifstream>(name.c_str(), std::ios::binary);
        if(!file->is_open()) file = nullptr;
        return std::move(file);
    }
};
static DefaultFileIOFactory sDefaultFileFactory;

static UniquePtr<FileIOFactory> sFileFactory;
UniquePtr<FileIOFactory> FileIOFactory::set(UniquePtr<FileIOFactory> factory)
{
    std::swap(sFileFactory, factory);
    return factory;
}

FileIOFactory &FileIOFactory::get()
{
    FileIOFactory *factory = sFileFactory.get();
    if(factory) return *factory;
    return sDefaultFileFactory;
}


// Default message handler methods are no-ops.
MessageHandler::~MessageHandler()
{
}

void MessageHandler::deviceDisconnected(Device*)
{
}

void MessageHandler::sourceStopped(Source*, bool)
{
}

void MessageHandler::bufferLoading(const String&, ChannelConfig, SampleType, ALuint, const Vector<ALbyte>&)
{
}

String MessageHandler::resourceNotFound(const String&)
{
    return String();
}


template<typename T>
static inline void LoadALFunc(T **func, const char *name)
{ *func = reinterpret_cast<T*>(alGetProcAddress(name)); }

static void LoadNothing(ALContext*) { }

static void LoadEFX(ALContext *ctx)
{
    LoadALFunc(&ctx->alGenEffects,    "alGenEffects");
    LoadALFunc(&ctx->alDeleteEffects, "alDeleteEffects");
    LoadALFunc(&ctx->alIsEffect,      "alIsEffect");
    LoadALFunc(&ctx->alEffecti,       "alEffecti");
    LoadALFunc(&ctx->alEffectiv,      "alEffectiv");
    LoadALFunc(&ctx->alEffectf,       "alEffectf");
    LoadALFunc(&ctx->alEffectfv,      "alEffectfv");
    LoadALFunc(&ctx->alGetEffecti,    "alGetEffecti");
    LoadALFunc(&ctx->alGetEffectiv,   "alGetEffectiv");
    LoadALFunc(&ctx->alGetEffectf,    "alGetEffectf");
    LoadALFunc(&ctx->alGetEffectfv,   "alGetEffectfv");

    LoadALFunc(&ctx->alGenFilters,    "alGenFilters");
    LoadALFunc(&ctx->alDeleteFilters, "alDeleteFilters");
    LoadALFunc(&ctx->alIsFilter,      "alIsFilter");
    LoadALFunc(&ctx->alFilteri,       "alFilteri");
    LoadALFunc(&ctx->alFilteriv,      "alFilteriv");
    LoadALFunc(&ctx->alFilterf,       "alFilterf");
    LoadALFunc(&ctx->alFilterfv,      "alFilterfv");
    LoadALFunc(&ctx->alGetFilteri,    "alGetFilteri");
    LoadALFunc(&ctx->alGetFilteriv,   "alGetFilteriv");
    LoadALFunc(&ctx->alGetFilterf,    "alGetFilterf");
    LoadALFunc(&ctx->alGetFilterfv,   "alGetFilterfv");

    LoadALFunc(&ctx->alGenAuxiliaryEffectSlots,    "alGenAuxiliaryEffectSlots");
    LoadALFunc(&ctx->alDeleteAuxiliaryEffectSlots, "alDeleteAuxiliaryEffectSlots");
    LoadALFunc(&ctx->alIsAuxiliaryEffectSlot,      "alIsAuxiliaryEffectSlot");
    LoadALFunc(&ctx->alAuxiliaryEffectSloti,       "alAuxiliaryEffectSloti");
    LoadALFunc(&ctx->alAuxiliaryEffectSlotiv,      "alAuxiliaryEffectSlotiv");
    LoadALFunc(&ctx->alAuxiliaryEffectSlotf,       "alAuxiliaryEffectSlotf");
    LoadALFunc(&ctx->alAuxiliaryEffectSlotfv,      "alAuxiliaryEffectSlotfv");
    LoadALFunc(&ctx->alGetAuxiliaryEffectSloti,    "alGetAuxiliaryEffectSloti");
    LoadALFunc(&ctx->alGetAuxiliaryEffectSlotiv,   "alGetAuxiliaryEffectSlotiv");
    LoadALFunc(&ctx->alGetAuxiliaryEffectSlotf,    "alGetAuxiliaryEffectSlotf");
    LoadALFunc(&ctx->alGetAuxiliaryEffectSlotfv,   "alGetAuxiliaryEffectSlotfv");
}

static void LoadSourceLatency(ALContext *ctx)
{
    LoadALFunc(&ctx->alGetSourcei64vSOFT, "alGetSourcei64vSOFT");
}

static const struct {
    enum ALExtension extension;
    const char name[32];
    void (&loader)(ALContext*);
} ALExtensionList[] = {
    { EXT_EFX, "ALC_EXT_EFX", LoadEFX },

    { EXT_FLOAT32,   "AL_EXT_FLOAT32",   LoadNothing },
    { EXT_MCFORMATS, "AL_EXT_MCFORMATS", LoadNothing },
    { EXT_BFORMAT,   "AL_EXT_BFORMAT",   LoadNothing },

    { EXT_MULAW,           "AL_EXT_MULAW",           LoadNothing },
    { EXT_MULAW_MCFORMATS, "AL_EXT_MULAW_MCFORMATS", LoadNothing },
    { EXT_MULAW_BFORMAT,   "AL_EXT_MULAW_BFORMAT",   LoadNothing },

    { SOFT_loop_points,    "AL_SOFT_loop_points",    LoadNothing },
    { SOFT_source_latency, "AL_SOFT_source_latency", LoadSourceLatency },

    { EXT_disconnect, "ALC_EXT_disconnect", LoadNothing },

    { EXT_SOURCE_RADIUS, "AL_EXT_SOURCE_RADIUS", LoadNothing },
    { EXT_STEREO_ANGLES, "AL_EXT_STEREO_ANGLES", LoadNothing },
};


ALContext *ALContext::sCurrentCtx = nullptr;
thread_local ALContext *ALContext::sThreadCurrentCtx = nullptr;

void ALContext::MakeCurrent(ALContext *context)
{
    std::unique_lock<std::mutex> lock1, lock2;
    if(sCurrentCtx)
        lock1 = std::unique_lock<std::mutex>(sCurrentCtx->mContextMutex);
    if(context && context != sCurrentCtx)
        lock2 = std::unique_lock<std::mutex>(context->mContextMutex);

    if(alcMakeContextCurrent(context ? context->getContext() : 0) == ALC_FALSE)
        throw std::runtime_error("Call to alcMakeContextCurrent failed");
    if(context)
    {
        context->addRef();
        std::call_once(context->mSetExts, std::mem_fn(&ALContext::setupExts), context);
    }
    std::swap(sCurrentCtx, context);
    if(context) context->decRef();

    if(sThreadCurrentCtx)
        sThreadCurrentCtx->decRef();
    sThreadCurrentCtx = 0;

    if(sCurrentCtx && sCurrentCtx != context)
    {
        lock2.unlock();
        sCurrentCtx->mWakeThread.notify_all();
    }
}

void ALContext::MakeThreadCurrent(ALContext *context)
{
    if(!ALDeviceManager::SetThreadContext)
        throw std::runtime_error("Thread-local contexts unsupported");
    if(ALDeviceManager::SetThreadContext(context ? context->getContext() : nullptr) == ALC_FALSE)
        throw std::runtime_error("Call to alcSetThreadContext failed");
    if(context)
    {
        context->addRef();
        std::call_once(context->mSetExts, std::mem_fn(&ALContext::setupExts), context);
    }
    if(sThreadCurrentCtx)
        sThreadCurrentCtx->decRef();
    sThreadCurrentCtx = context;
}

void ALContext::setupExts()
{
    ALCdevice *device = mDevice->getDevice();
    std::fill(std::begin(mHasExt), std::end(mHasExt), false);
    for(const auto &entry : ALExtensionList)
    {
        mHasExt[entry.extension] = (strncmp(entry.name, "ALC", 3) == 0) ?
                                   alcIsExtensionPresent(device, entry.name) :
                                   alIsExtensionPresent(entry.name);
        if(mHasExt[entry.extension]) entry.loader(this);
    }
}


void ALContext::backgroundProc()
{
    if(ALDeviceManager::SetThreadContext && mDevice->hasExtension(EXT_thread_local_context))
        ALDeviceManager::SetThreadContext(getContext());

    std::chrono::steady_clock::time_point basetime = std::chrono::steady_clock::now();
    std::chrono::milliseconds waketime(0);
    std::unique_lock<std::mutex> ctxlock(mContextMutex);
    while(!mQuitThread.load(std::memory_order_acquire))
    {
        {
            std::lock_guard<std::mutex> srclock(mSourceStreamMutex);
            auto source = mStreamingSources.begin();
            while(source != mStreamingSources.end())
            {
                if(!(*source)->updateAsync())
                    source = mStreamingSources.erase(source);
                else
                    ++source;
            }
        }

        // Only do one pending buffer at a time. In case there's several large
        // buffers to load, we still need to process streaming sources so they
        // don't underrun.
        RingBuffer::Data ringdata = mPendingBuffers.get_read_vector()[0];
        if(ringdata.len > 0)
        {
            PendingBuffer *pb = reinterpret_cast<PendingBuffer*>(ringdata.buf);
            pb->mBuffer->load(pb->mFrames, pb->mFormat, pb->mDecoder, pb->mName, this);
            pb->~PendingBuffer();
            mPendingBuffers.read_advance(1);
            continue;
        }

        std::unique_lock<std::mutex> wakelock(mWakeMutex);
        if(!mQuitThread.load(std::memory_order_acquire) && mPendingBuffers.read_space() == 0)
        {
            ctxlock.unlock();

            ALuint interval = mWakeInterval.load();
            if(!interval)
                mWakeThread.wait(wakelock);
            else
            {
                auto now = std::chrono::steady_clock::now() - basetime;
                auto duration = std::chrono::milliseconds(interval);
                while((waketime - now).count() <= 0) waketime += duration;
                mWakeThread.wait_until(wakelock, waketime + basetime);
            }
            wakelock.unlock();

            ctxlock.lock();
            while(!mQuitThread.load(std::memory_order_acquire) &&
                  alcGetCurrentContext() != getContext())
                mWakeThread.wait(ctxlock);
        }
    }
    ctxlock.unlock();

    if(ALDeviceManager::SetThreadContext)
        ALDeviceManager::SetThreadContext(nullptr);
}


ALContext::ALContext(ALCcontext *context, ALDevice *device)
  : mContext(context), mDevice(device), mRefs(0),
    mHasExt{false}, mPendingBuffers(16, sizeof(PendingBuffer)),
    mWakeInterval(0), mQuitThread(false), mIsConnected(true), mIsBatching(false),
    alGetSourcei64vSOFT(0),
    alGenEffects(0), alDeleteEffects(0), alIsEffect(0),
    alEffecti(0), alEffectiv(0), alEffectf(0), alEffectfv(0),
    alGetEffecti(0), alGetEffectiv(0), alGetEffectf(0), alGetEffectfv(0),
    alGenFilters(0), alDeleteFilters(0), alIsFilter(0),
    alFilteri(0), alFilteriv(0), alFilterf(0), alFilterfv(0),
    alGetFilteri(0), alGetFilteriv(0), alGetFilterf(0), alGetFilterfv(0),
    alGenAuxiliaryEffectSlots(0), alDeleteAuxiliaryEffectSlots(0), alIsAuxiliaryEffectSlot(0),
    alAuxiliaryEffectSloti(0), alAuxiliaryEffectSlotiv(0), alAuxiliaryEffectSlotf(0), alAuxiliaryEffectSlotfv(0),
    alGetAuxiliaryEffectSloti(0), alGetAuxiliaryEffectSlotiv(0), alGetAuxiliaryEffectSlotf(0), alGetAuxiliaryEffectSlotfv(0)
{
}

ALContext::~ALContext()
{
    auto ringdata = mPendingBuffers.get_read_vector();
    if(ringdata[0].len > 0)
    {
        PendingBuffer *pb = reinterpret_cast<PendingBuffer*>(ringdata[0].buf);
        for(size_t i = 0;i < ringdata[0].len;i++)
            pb[i].~PendingBuffer();
        pb = reinterpret_cast<PendingBuffer*>(ringdata[1].buf);
        for(size_t i = 0;i < ringdata[1].len;i++)
            pb[i].~PendingBuffer();

        mPendingBuffers.read_advance(ringdata[0].len + ringdata[1].len);
    }
}


Device *ALContext::getDevice()
{
    return mDevice;
}

void ALContext::destroy()
{
    if(mRefs.load() != 0)
        throw std::runtime_error("Context is in use");
    if(!mBuffers.empty())
        throw std::runtime_error("Trying to destroy a context with buffers");

    if(mThread.joinable())
    {
        std::unique_lock<std::mutex> lock(mWakeMutex);
        mQuitThread.store(true, std::memory_order_release);
        lock.unlock();
        mWakeThread.notify_all();
        mThread.join();
    }

    alcDestroyContext(mContext);
    mContext = nullptr;

    mDevice->removeContext(this);
}


void ALContext::startBatch()
{
    alcSuspendContext(mContext);
    mIsBatching = true;
}

void ALContext::endBatch()
{
    alcProcessContext(mContext);
    mIsBatching = false;
}


Listener *ALContext::getListener()
{
    return this;
}


SharedPtr<MessageHandler> ALContext::setMessageHandler(SharedPtr<MessageHandler> handler)
{
    std::lock_guard<std::mutex> lock(mContextMutex);
    mMessage.swap(handler);
    return handler;
}


void ALContext::setAsyncWakeInterval(ALuint msec)
{
    mWakeInterval.store(msec);
    mWakeMutex.lock(); mWakeMutex.unlock();
    mWakeThread.notify_all();
}

ALuint ALContext::getAsyncWakeInterval() const
{
    return mWakeInterval.load();
}


SharedPtr<Decoder> ALContext::createDecoder(const String &name)
{
    auto file = FileIOFactory::get().openFile(name);
    if(file) return GetDecoder(name, std::move(file));

    // Resource not found. Try to find a substitute.
    if(!mMessage.get()) throw std::runtime_error("Failed to open "+name);
    String oldname = name;
    do {
        String newname(mMessage->resourceNotFound(oldname));
        if(newname.empty())
            throw std::runtime_error("Failed to open "+oldname);
        file = FileIOFactory::get().openFile(newname);
        oldname = std::move(newname);
    } while(!file);

    return GetDecoder(oldname, std::move(file));
}


bool ALContext::isSupported(ChannelConfig channels, SampleType type) const
{
    return GetFormat(channels, type) != AL_NONE;
}


Buffer *ALContext::getBuffer(const String &name)
{
    CheckContext(this);

    auto hasher = std::hash<String>();
    auto iter = std::lower_bound(mBuffers.begin(), mBuffers.end(), hasher(name),
        [hasher](const UniquePtr<ALBuffer> &lhs, size_t rhs) -> bool
        { return hasher(lhs->getName()) < rhs; }
    );
    if(iter != mBuffers.end() && (*iter)->getName() == name)
    {
        // Ensure the buffer is loaded before returning. getBuffer guarantees
        // the returned buffer is loaded.
        ALBuffer *buffer = iter->get();
        while(buffer->getLoadStatus() == BufferLoadStatus::Pending)
            std::this_thread::yield();
        return buffer;
    }
    // NOTE: 'iter' is used later to insert a new entry!

    auto decoder = createDecoder(name);

    ALuint srate = decoder->getFrequency();
    ChannelConfig chans = decoder->getChannelConfig();
    SampleType type = decoder->getSampleType();
    ALuint frames = decoder->getLength();

    Vector<ALbyte> data(FramesToBytes(frames, chans, type));
    frames = decoder->read(&data[0], frames);
    if(!frames) throw std::runtime_error("No samples for buffer");
    data.resize(FramesToBytes(frames, chans, type));

    std::pair<uint64_t,uint64_t> loop_pts = decoder->getLoopPoints();
    if(loop_pts.first >= loop_pts.second)
        loop_pts = std::make_pair(0, frames);
    else
    {
        loop_pts.second = std::min<uint64_t>(loop_pts.second, frames);
        loop_pts.first = std::min<uint64_t>(loop_pts.first, loop_pts.second-1);
    }

    // Get the format before calling the bufferLoading message handler, to
    // ensure it's something OpenAL can handle.
    ALenum format = GetFormat(chans, type);
    if(format == AL_NONE)
    {
        std::stringstream sstr;
        sstr<< "Format not supported ("<<GetSampleTypeName(type)<<", "<<GetChannelConfigName(chans)<<")";
        throw std::runtime_error(sstr.str());
    }

    if(mMessage.get())
        mMessage->bufferLoading(name, chans, type, srate, data);

    alGetError();
    ALuint bid = 0;
    try {
        alGenBuffers(1, &bid);
        alBufferData(bid, format, &data[0], data.size(), srate);
        if(hasExtension(SOFT_loop_points))
        {
            ALint pts[2]{(ALint)loop_pts.first, (ALint)loop_pts.second};
            alBufferiv(bid, AL_LOOP_POINTS_SOFT, pts);
        }
        if(alGetError() != AL_NO_ERROR)
            throw std::runtime_error("Failed to buffer data");

        return mBuffers.insert(iter,
            MakeUnique<ALBuffer>(this, bid, srate, chans, type, true, name)
        )->get();
    }
    catch(...) {
        alDeleteBuffers(1, &bid);
        throw;
    }
}

Buffer *ALContext::getBufferAsync(const String &name)
{
    CheckContext(this);

    auto hasher = std::hash<String>();
    auto iter = std::lower_bound(mBuffers.begin(), mBuffers.end(), hasher(name),
        [hasher](const UniquePtr<ALBuffer> &lhs, size_t rhs) -> bool
        { return hasher(lhs->getName()) < rhs; }
    );
    if(iter != mBuffers.end() && (*iter)->getName() == name)
        return iter->get();
    // NOTE: 'iter' is used later to insert a new entry!

    auto decoder = createDecoder(name);

    ALuint srate = decoder->getFrequency();
    ChannelConfig chans = decoder->getChannelConfig();
    SampleType type = decoder->getSampleType();
    ALuint frames = decoder->getLength();
    if(!frames) throw std::runtime_error("No samples for buffer");

    ALenum format = GetFormat(chans, type);
    if(format == AL_NONE)
    {
        std::stringstream sstr;
        sstr<< "Format not supported ("<<GetSampleTypeName(type)<<", "<<GetChannelConfigName(chans)<<")";
        throw std::runtime_error(sstr.str());
    }

    alGetError();
    ALuint bid = 0;
    alGenBuffers(1, &bid);
    if(alGetError() != AL_NO_ERROR)
        throw std::runtime_error("Failed to buffer data");

    auto buffer = MakeUnique<ALBuffer>(this, bid, srate, chans, type, false, name);

    if(mThread.get_id() == std::thread::id())
        mThread = std::thread(std::mem_fn(&ALContext::backgroundProc), this);

    while(mPendingBuffers.write_space() == 0)
        std::this_thread::yield();

    RingBuffer::Data ringdata = mPendingBuffers.get_write_vector()[0];
    new(ringdata.buf) PendingBuffer{name, buffer.get(), decoder, format, frames};
    mPendingBuffers.write_advance(1);
    mWakeMutex.lock(); mWakeMutex.unlock();
    mWakeThread.notify_all();

    return mBuffers.insert(iter, std::move(buffer))->get();
}


void ALContext::removeBuffer(const String &name)
{
    CheckContext(this);
    auto hasher = std::hash<String>();
    auto iter = std::lower_bound(mBuffers.begin(), mBuffers.end(), hasher(name),
        [hasher](const UniquePtr<ALBuffer> &lhs, size_t rhs) -> bool
        { return hasher(lhs->getName()) < rhs; }
    );
    if(iter != mBuffers.end() && (*iter)->getName() == name)
    {
        (*iter)->cleanup();
        mBuffers.erase(iter);
    }
}

void ALContext::removeBuffer(Buffer *buffer)
{
    removeBuffer(buffer->getName());
}


ALuint ALContext::getSourceId(ALuint maxprio)
{
    CheckContext(this);

    ALuint id = 0;
    if(mSourceIds.empty())
    {
        alGetError();
        alGenSources(1, &id);
        if(alGetError() == AL_NO_ERROR)
            return id;

        ALSource *lowest = nullptr;
        for(ALSource *src : mUsedSources)
        {
            if(src->getId() != 0 && (!lowest || src->getPriority() < lowest->getPriority()))
                lowest = src;
        }
        if(lowest && lowest->getPriority() < maxprio)
        {
            lowest->makeStopped();
            if(mMessage.get())
                mMessage->sourceStopped(lowest, true);
        }
    }
    if(mSourceIds.empty())
        throw std::runtime_error("No available sources");

    id = mSourceIds.top();
    mSourceIds.pop();
    return id;
}


Source *ALContext::createSource()
{
    CheckContext(this);

    ALSource *source;
    if(!mFreeSources.empty())
    {
        source = mFreeSources.back();
        mFreeSources.pop();
    }
    else
    {
        mAllSources.emplace_back(this);
        source = &mAllSources.back();
    }
    auto iter = std::lower_bound(mUsedSources.begin(), mUsedSources.end(), source);
    if(iter == mUsedSources.end() || *iter != source)
        mUsedSources.insert(iter, source);
    return source;
}

void ALContext::freeSource(ALSource *source)
{
    auto iter = std::lower_bound(mUsedSources.begin(), mUsedSources.end(), source);
    if(iter != mUsedSources.end() && *iter == source) mUsedSources.erase(iter);
    mFreeSources.push(source);
}


void ALContext::addStream(ALSource *source)
{
    std::lock_guard<std::mutex> lock(mSourceStreamMutex);
    if(mThread.get_id() == std::thread::id())
        mThread = std::thread(std::mem_fn(&ALContext::backgroundProc), this);
    auto iter = std::lower_bound(mStreamingSources.begin(), mStreamingSources.end(), source);
    if(iter == mStreamingSources.end() || *iter != source)
        mStreamingSources.insert(iter, source);
}

void ALContext::removeStream(ALSource *source)
{
    std::lock_guard<std::mutex> lock(mSourceStreamMutex);
    auto iter = std::lower_bound(mStreamingSources.begin(), mStreamingSources.end(), source);
    if(iter != mStreamingSources.end() && *iter == source)
        mStreamingSources.erase(iter);
}

void ALContext::removeStreamNoLock(ALSource *source)
{
    auto iter = std::lower_bound(mStreamingSources.begin(), mStreamingSources.end(), source);
    if(iter != mStreamingSources.end() && *iter == source)
        mStreamingSources.erase(iter);
}


AuxiliaryEffectSlot *ALContext::createAuxiliaryEffectSlot()
{
    if(!hasExtension(EXT_EFX) || !alGenAuxiliaryEffectSlots)
        throw std::runtime_error("AuxiliaryEffectSlots not supported");
    CheckContext(this);

    alGetError();
    ALuint id = 0;
    alGenAuxiliaryEffectSlots(1, &id);
    if(alGetError() != AL_NO_ERROR)
        throw std::runtime_error("Failed to create AuxiliaryEffectSlot");
    try {
        return new ALAuxiliaryEffectSlot(this, id);
    }
    catch(...) {
        alDeleteAuxiliaryEffectSlots(1, &id);
        throw;
    }
}


Effect *ALContext::createEffect()
{
    if(!hasExtension(EXT_EFX))
        throw std::runtime_error("Effects not supported");
    CheckContext(this);

    alGetError();
    ALuint id = 0;
    alGenEffects(1, &id);
    if(alGetError() != AL_NO_ERROR)
        throw std::runtime_error("Failed to create Effect");
    try {
        return new ALEffect(this, id);
    }
    catch(...) {
        alDeleteEffects(1, &id);
        throw;
    }
}


SourceGroup *ALContext::createSourceGroup(String name)
{
    auto iter = std::lower_bound(mSourceGroups.begin(), mSourceGroups.end(), name,
        [](const UniquePtr<ALSourceGroup> &lhs, const String &rhs) -> bool
        { return lhs->getName() < rhs; }
    );
    if(iter != mSourceGroups.end() && (*iter)->getName() == name)
        throw std::runtime_error("Duplicate source group name");
    iter = mSourceGroups.insert(iter, MakeUnique<ALSourceGroup>(this, std::move(name)));
    return iter->get();
}

SourceGroup *ALContext::getSourceGroup(const String &name)
{
    auto iter = std::lower_bound(mSourceGroups.begin(), mSourceGroups.end(), name,
        [](const UniquePtr<ALSourceGroup> &lhs, const String &rhs) -> bool
        { return lhs->getName() < rhs; }
    );
    if(iter == mSourceGroups.end() || (*iter)->getName() != name)
        throw std::runtime_error("Source group not found");
    return iter->get();
}

void ALContext::freeSourceGroup(ALSourceGroup *group)
{
    auto iter = std::lower_bound(mSourceGroups.begin(), mSourceGroups.end(), group->getName(),
        [](const UniquePtr<ALSourceGroup> &lhs, const String &rhs) -> bool
        { return lhs->getName() < rhs; }
    );
    if(iter != mSourceGroups.end() && iter->get() == group)
        mSourceGroups.erase(iter);
}


void ALContext::setDopplerFactor(ALfloat factor)
{
    if(!(factor >= 0.0f))
        throw std::runtime_error("Doppler factor out of range");
    CheckContext(this);
    alDopplerFactor(factor);
}


void ALContext::setSpeedOfSound(ALfloat speed)
{
    if(!(speed > 0.0f))
        throw std::runtime_error("Speed of sound out of range");
    CheckContext(this);
    alSpeedOfSound(speed);
}


void ALContext::setDistanceModel(DistanceModel model)
{
    CheckContext(this);
    alDistanceModel((ALenum)model);
}


void ALContext::update()
{
    CheckContext(this);
    std::for_each(mUsedSources.begin(), mUsedSources.end(), std::mem_fn(&ALSource::updateNoCtxCheck));
    if(!mWakeInterval.load())
    {
        // For performance reasons, don't wait for the thread's mutex. This
        // should be called often enough to keep up with any and all streams
        // regardless.
        mWakeThread.notify_all();
    }

    if(hasExtension(EXT_disconnect) && mIsConnected)
    {
        ALCint connected;
        alcGetIntegerv(alcGetContextsDevice(mContext), ALC_CONNECTED, 1, &connected);
        if(!connected && mMessage.get()) mMessage->deviceDisconnected(mDevice);
        mIsConnected = connected;
    }
}


void ALContext::setGain(ALfloat gain)
{
    if(!(gain >= 0.0f))
        throw std::runtime_error("Gain out of range");
    CheckContext(this);
    alListenerf(AL_GAIN, gain);
}


void ALContext::setPosition(ALfloat x, ALfloat y, ALfloat z)
{
    CheckContext(this);
    alListener3f(AL_POSITION, x, y, z);
}

void ALContext::setPosition(const ALfloat *pos)
{
    CheckContext(this);
    alListenerfv(AL_POSITION, pos);
}

void ALContext::setVelocity(ALfloat x, ALfloat y, ALfloat z)
{
    CheckContext(this);
    alListener3f(AL_VELOCITY, x, y, z);
}

void ALContext::setVelocity(const ALfloat *vel)
{
    CheckContext(this);
    alListenerfv(AL_VELOCITY, vel);
}

void ALContext::setOrientation(ALfloat x1, ALfloat y1, ALfloat z1, ALfloat x2, ALfloat y2, ALfloat z2)
{
    CheckContext(this);
    ALfloat ori[6] = { x1, y1, z1, x2, y2, z2 };
    alListenerfv(AL_ORIENTATION, ori);
}

void ALContext::setOrientation(const ALfloat *at, const ALfloat *up)
{
    CheckContext(this);
    ALfloat ori[6] = { at[0], at[1], at[2], up[0], up[1], up[2] };
    alListenerfv(AL_ORIENTATION, ori);
}

void ALContext::setOrientation(const ALfloat *ori)
{
    CheckContext(this);
    alListenerfv(AL_ORIENTATION, ori);
}

void ALContext::setMetersPerUnit(ALfloat m_u)
{
    if(!(m_u > 0.0f))
        throw std::runtime_error("Invalid meters per unit");
    CheckContext(this);
    if(hasExtension(EXT_EFX))
        alListenerf(AL_METERS_PER_UNIT, m_u);
}


void Context::MakeCurrent(Context *context)
{
    ALContext *ctx = nullptr;
    if(context)
    {
        ctx = cast<ALContext*>(context);
        if(!ctx) throw std::runtime_error("Invalid context pointer");
    }
    ALContext::MakeCurrent(ctx);
}

Context *Context::GetCurrent()
{
    return ALContext::GetCurrent();
}

void Context::MakeThreadCurrent(Context *context)
{
    ALContext *ctx = nullptr;
    if(context)
    {
        ctx = cast<ALContext*>(context);
        if(!ctx) throw std::runtime_error("Invalid context pointer");
    }
    ALContext::MakeThreadCurrent(ctx);
}

Context *Context::GetThreadCurrent()
{
    return ALContext::GetThreadCurrent();
}

}
