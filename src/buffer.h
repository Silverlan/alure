#ifndef BUFFER_H
#define BUFFER_H

#include "main.h"

#include <algorithm>

#include "al.h"

#include "refcount.h"

namespace alure {

class ALContext;


ALuint FramesToBytes(ALuint size, ChannelConfig chans, SampleType type);
ALenum GetFormat(ChannelConfig chans, SampleType type);


class ALBuffer : public Buffer {
    ALContext *const mContext;
    ALuint mId;

    ALuint mFrequency;
    ChannelConfig mChannelConfig;
    SampleType mSampleType;

    BufferLoadStatus mLoadStatus;
    volatile bool mIsLoaded;

    std::vector<Source*> mSources;

public:
    ALBuffer(ALContext *context, ALuint id, ALuint freq, ChannelConfig config, SampleType type, bool preloaded)
      : mContext(context), mId(id), mFrequency(freq), mChannelConfig(config), mSampleType(type),
        mLoadStatus(preloaded?BufferLoad_Ready:BufferLoad_Pending), mIsLoaded(preloaded)
    { }
    virtual ~ALBuffer() { }

    void cleanup();

    ALContext *getContext() { return mContext; }
    ALuint getId() const { return mId; }

    void addSource(Source *source) { mSources.push_back(source); }
    void removeSource(Source *source)
    {
        auto iter = std::find(mSources.cbegin(), mSources.cend(), source);
        if(iter != mSources.cend()) mSources.erase(iter);
    }

    void load(ALuint frames, ALenum format, SharedPtr<Decoder> decoder, const std::string &name, ALContext *ctx);

    bool isReady() const { return mLoadStatus == BufferLoad_Ready; }

    virtual ALuint getLength() const final;

    virtual ALuint getFrequency() const final { return mFrequency; }
    virtual ChannelConfig getChannelConfig() const final { return mChannelConfig; }
    virtual SampleType getSampleType() const final { return mSampleType; }

    virtual ALuint getSize() const final;

    virtual void setLoopPoints(ALuint start, ALuint end) final;
    virtual std::pair<ALuint,ALuint> getLoopPoints() const final;

    virtual std::vector<Source*> getSources() const final
    { return mSources; }

    virtual BufferLoadStatus getLoadStatus() final;

    virtual bool isInUse() const final { return (mSources.size() > 0); }
};

} // namespace alure

#endif /* BUFFER_H */
