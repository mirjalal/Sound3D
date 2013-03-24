/**
 * Sound3D - An open source 3D Audio Library
 * 
 * Copyright (c) 2013 Jorma Rebane
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software 
 * and associated documentation files (the "Software"), to deal in the Software without restriction, 
 * including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, 
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, 
 * subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all copies or substantial 
 * portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT 
 * LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN 
 * NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, 
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE 
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "Sound3D.h"
#include <stdio.h>
#include <string.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "OpenAL\al.h"
#include "OpenAL\alc.h"

#ifdef _DEBUG
#define indebug(x) x
#elif
#define indebug(x) // ...
#endif

namespace S3D {

static ALCdevice* pDevice = NULL;
static ALCcontext* pContext = NULL;
static void _UninitAL()
{
	alcMakeContextCurrent(NULL); // remove current context
	alcDestroyContext(pContext); // destroy context
	alcCloseDevice(pDevice); // close device
}
static void _InitializeAL()
{
	pDevice = alcOpenDevice(NULL);
	pContext = alcCreateContext(pDevice, NULL);
	alcMakeContextCurrent(pContext); // set current active context
	_Atexit(_UninitAL);
}

	/**
	 * @return OpenAL specific format specifier based on numChannels and sampleSize
	 */
	static int GetALFormat(int numChannels, int sampleSize)
	{
		return numChannels == 1 ? 
			(sampleSize == 1 ? AL_FORMAT_MONO8 : AL_FORMAT_MONO16) : 
			(sampleSize == 1 ? AL_FORMAT_STEREO8 : AL_FORMAT_STEREO16);
	}

	/**
	 * @param size Size of the OpenAL buffer to create and fill with audio data
	 * @param strm AudioStream to stream from
	 * @param pos [optional] Position of the stream to stream from. Returns the new position of the stream. (in BYTES)
	 * @return OpenAL buffer that was created and filled
	 */
	static int CreateALBuffer(int size, AudioStreamer* strm, int* pos = nullptr)
	{
		//if(pos && strm->Position() != *pos)
		if(pos)	strm->Seek(*pos); // seek to specified pos, let the AudioStream handle error conditions
		unsigned alBuffer = 0;
		alGenBuffers(1, &alBuffer);

		char* data = (char*)_malloca(size);
		int bytesRead = strm->ReadSome(data, size);
		if(pos) *pos += bytesRead; // update position
		alBufferData(alBuffer, GetALFormat(strm->Channels(), strm->SampleSize()), data, bytesRead, strm->Frequency());
		_freea(data);
		return alBuffer;
	}

	/**
	 * @param alBuffer OpenAL buffer to fill with audio data
	 * @param strm AudioStream to stream from
	 * @param pos [optional] Position of the stream to stream from (in BYTES)
	 */
	static void FillALBuffer(unsigned alBuffer, AudioStreamer* strm, int* pos = nullptr)
	{
		//if(pos && strm->Position() != *pos)
		if(pos)	strm->Seek(*pos); // seek to specified pos, let the AudioStream handle error conditions
		int size;
		alGetBufferi(alBuffer, AL_SIZE, &size);
		char* data = (char*)_malloca(size);
		int bytesRead = strm->ReadSome(data, size);
		if(pos) *pos += bytesRead; // update position
		alBufferData(alBuffer, GetALFormat(strm->Channels(), strm->SampleSize()), data, bytesRead, strm->Frequency());
		_freea(data);
	}





	SoundBuffer::SoundBuffer() // creates a new SoundBuffer object
		: alBuffer(0), refCount(0)
	{
		if(!pDevice) _InitializeAL();
	}
	SoundBuffer::~SoundBuffer() // destroys and unloads this buffer
	{
		if(alBuffer) {
			Unload();
		}
	}
	int SoundBuffer::Frequency() const		// @return Frequency in Hz of this SoundBuffer data
	{
		int value; alGetBufferi(alBuffer, AL_FREQUENCY, &value);
		return value;
	}
	int SoundBuffer::SampleBits() const		// @return Bitrate of this SoundBuffer data
	{
		int value; alGetBufferi(alBuffer, AL_BITS, &value);
		return value;
	}
	int SoundBuffer::SampleBytes() const	// @return Number of bytes in a sample of this SoundBuffer data (1 or 2)
	{
		return SampleBits() >> 3; // div by 8
	}
	int SoundBuffer::Channels() const		// @return Number of sound channels in the SoundBuffer data (1 or 2)
	{
		int value; alGetBufferi(alBuffer, AL_CHANNELS, &value);
		return value;
	}
	int SoundBuffer::SampleSize() const		// @return Size of a sample block in bytes [LL][RR] (2 or 4)
	{
		return SampleBytes() * Channels();
	}

	/**
	 * @note For a SoundStream object, this returns the entire stream size in bytes!
	 * @return Size of this SoundBuffer in PCM SAMPLES
	 */
	int SoundBuffer::Size() const
	{
		int value; alGetBufferi(alBuffer, AL_SIZE, &value);
		return value / (SampleBytes() * Channels());
	}

	/**
	 * Loads this SoundBuffer with data found in the specified file.
	 * Supported formats: .wav .mp3
	 * @param file Sound file to load
	 * @return TRUE if loading succeeded and a valid buffer was created.
	 */
	bool SoundBuffer::Load(const char* file)
	{
		if(alBuffer) // is there existing data?
			return false;
		
		AudioStreamer mem;
		AudioStreamer* strm = &mem;
		if(!CreateAudioStreamer(strm, file))
			return false; // invalid file format or file not found

		if(!strm->OpenStream(file))
			return false; // failed to open the stream (probably not really correct format)

		alBuffer = CreateALBuffer(strm->Size(), strm);
		strm->CloseStream(); // close this manually, otherwise we get a nasty error when the dtor runs...
		return true;
	}

	/**
	 * Tries to release the underlying sound buffer and free the memory.
	 * @note This function will fail if refCount > 0. This means there are SoundObjects still using this SoundBuffer
	 * @return TRUE if SoundBuffer was freed, FALSE if SoundBuffer is still used by a SoundObject.
	 */
	bool SoundBuffer::Unload()
	{
		if(!alBuffer)
			return true; // yes, its unloaded
		if(refCount > 0) {
			indebug(printf("SoundBuffer::Unload() Memory Leak: failed to delete alBuffer, because it's still in use.\n"));
			return false; // can't do anything here while still referenced
		}
		alBufferData(alBuffer, AL_FORMAT_MONO8, NULL, 0, 0); // remove buffer data
		alDeleteBuffers(1, &alBuffer);
		alBuffer = 0;
		return true;
	}

	/**
	 * Binds a specific source to this SoundBuffer and increases the refCount.
	 * @param so SoundObject to bind to this SoundBuffer.
	 * @return FALSE is the binding failed. TODO: POSSIBLE REASONS?
	 */
	bool SoundBuffer::BindSource(SoundObject* so)
	{
		if(!alBuffer) 
			return false; // no data

		int value;
		alGetSourcei(so->alSource, AL_BUFFER, &value);
		if(value == alBuffer) // same buffer? no double-binding dude, it will mess up refCounting.
			return false;

		alSourcei(so->alSource, AL_BUFFER, alBuffer);
		++refCount;
		return true;
	}

	/**
	 * Unbinds a specific source from this SoundBuffer and decreases the refCount.
	 * @param so SoundObject to unbind from this SoundBuffer.
	 * @return FALSE if the unbinding failed. TODO: POSSIBLE REASONS?
	 */
	bool SoundBuffer::UnbindSource(SoundObject* so)
	{
		int value;
		alGetSourcei(so->alSource, AL_BUFFER, &value); // correct alSource <-> alBuffer link?
		if(value == alBuffer) {
			so->Stop(); // stop the sound before unloading
			alSourcei(so->alSource, AL_BUFFER, NULL); // detach the buffer
			--refCount;
			return true;
		}
		return false;
	}












	SoundStream::SoundStream() // creates a new SoundStream object
		//: alStreamBuffers()
	{
	}
	SoundStream::~SoundStream() // destroys and unloads this SoundStream
	{
		if(alBuffer) // active buffer?
			Unload();
	}

	/**
	 * @note For a SoundStream object, this returns the entire stream size in bytes!
	 * @return Size of this SoundStream in PCM SAMPLES
	 */
	int SoundStream::Size() const
	{
		return alStream ? (alStream->Size() / (SampleBytes() * Channels())) : 0;
	}

	/**
	 * Initializes this SoundStream with data found in the specified file.
	 * Supported formats: .wav .mp3
	 * @param file Sound file to load
	 * @return TRUE if loading succeeded and a stream was initialized.
	 */
	bool SoundStream::Load(const char* file)
	{
		if(alBuffer) // is there existing data?
			return false;

		if(!(alStream = CreateAudioStreamer(file)))
			return false; // :(
		
		if(!alStream->OpenStream(file))
			return false;

		int pos = 0;
		alBuffer = CreateALBuffer(STREAM_BUFFERSIZE, alStream, &pos);
		return true;
	}

	/**
	 * Tries to release the underlying sound buffers and free the memory.
	 * @note This function will fail if refCount > 0. This means there are SoundObjects still using this SoundStream
	 * @return TRUE if SoundStream data was freed, FALSE if SoundStream is still used by a SoundObject.
	 */
	bool SoundStream::Unload()
	{
		if(!alBuffer)
			return true; // yes, its unloaded
		if(refCount > 0) {
			indebug(printf("SoundStream::Unload() Memory Leak: failed to delete alBuffer, because it's still in use.\n"));
			return false; // can't do anything here while still referenced
		}
		alDeleteBuffers(1, (ALuint*)&alBuffer);
		alBuffer = 0;
		if(alStream) { delete alStream; alStream = 0; }
		return true;
	}

	/**
	 * Binds a specific source to this SoundStream and increases the refCount.
	 * @param so SoundObject to bind to this SoundStream.
	 * @return FALSE is the binding failed. TODO: POSSIBLE REASONS?
	 */
	bool SoundStream::BindSource(SoundObject* so)
	{
		if(!alBuffer) 
			return false; // no data loaded yet

		alSources.push_back(SO_ENTRY(so, 0, 0)); // default streamPos
		LoadStreamData(so, 0); // load initial stream data (2 buffers)

		++refCount;
		return true;
	}

	/**
	 * Unbinds a specific source from this SoundStream and decreases the refCount.
	 * @param so SoundObject to unbind from this SoundStream.
	 * @return FALSE if the unbinding failed. TODO: POSSIBLE REASONS?
	 */
	bool SoundStream::UnbindSource(SoundObject* so)
	{
		if(!alBuffer)
			return false; // no data loaded yet
		
		int index = GetIndexOf(so);
		if(index == -1) return false; // source doesn't exist

		so->Stop(); // stop the sound before unloading
		ClearStreamData(so); // unload all buffers

		alSources.erase(alSources.begin() + index);
		--refCount;
		return true;
	}

	/**
	 * Streams more sound data from the AudioStream if needed.
	 * This method can be called several times with no impact - the buffers
	 * are only filled ON-DEMAND. This method is simply the trigger mechanism.
	 * All the bound SoundObjects are streamed and updated.
	 * @return Number of buffers streamed and filled.
	 */
	int SoundStream::Stream()
	{
		if(!alBuffer) return 0; // nothing to do here
		int numStreamed = 0;
		for(auto it = alSources.begin(); it != alSources.end(); ++it)
			numStreamed += Stream(*it);
		return numStreamed;
	}

	/**
	 * Streams more sound data from the AudioStream if needed.
	 * This method can be called several times with no impact - the buffers
	 * are only filled ON-DEMAND. This method is simply the trigger mechanism.
	 * @param so Specific SoundObject to stream with.
	 * @return Number of buffers streamed and filled.
	 */
	int SoundStream::Stream(SoundObject* so)
	{
		if(!alBuffer) return 0; // nothing to do here
		int index = GetIndexOf(so);
		if(index == -1) return 0;
		return Stream(alSources[index]);
	}

	/**
	 * @param so Specific SoundObject to check for end of stream
	 * @return TRUE if End of Stream was reached or if there is no stream loaded
	 */
	bool SoundStream::IsEOS(const SoundObject* so) const
	{
		int i = GetIndexOf(so);
		return i == -1 ? true : (alStream ? (alSources[i].next >= (unsigned)alStream->Size()) : true);
	}

	/**
	 * @param so SoundObject to query index of
	 * @return A valid index [0...n] if this SoundObject is bound. [-1] if it's not bound.
	 */
	int SoundStream::GetIndexOf(const SoundObject* so) const
	{
		for(unsigned i = 0; i < alSources.size(); ++i)
			if(alSources[i].obj == so)
				return i;
		return -1;
	}

	/**
	 * Resets the stream by unloading previous buffers and requeuing the first two buffers.
	 * @param so SoundObject to reset the stream for
	 */
	void SoundStream::ResetStream(SoundObject* so)
	{
		so->Stop();
		// simply clear and load again, to avoid code maintenance hell
		ClearStreamData(so);
		LoadStreamData(so, 0);
	}

	/**
	 * Seeks to the specified sample position in the stream
	 * @param so SoundObject to perform seek on
	 * @param samplepos Position in the stream in samples [0..SoundStream::Size()]
	 */
	void SoundStream::Seek(SoundObject* so, int samplepos)
	{
		bool wasPlaying = so->IsPlaying();
		// if samplepos out of bounds THEN 0 ELSE convert samplepos to bytepos
		int bytepos = (samplepos >= Size()) ? 0 : samplepos * SampleSize();
		ClearStreamData(so);
		LoadStreamData(so, bytepos);
		if(wasPlaying) so->Play(); // resume playback
	}

	/**
	 * Gets the current sample position of the stream in the context of the bound SoundObject
	 * @param so SoundObject to get sample position of
	 * @return Sample position of the specified SoundObject
	 */
	int SoundStream::GetSamplePos(const SoundObject*const so) const
	{
		int index = GetIndexOf(so);
		if(index == -1 || !alStream) return 0;
		int bufferPos ; alGetSourcei(so->alSource, AL_BYTE_OFFSET, &bufferPos); // current offset in bytes
		// add offsetting bufferPos and then convert to PCM Samples
		return (alSources[index].base + bufferPos) / alStream->SampleSize();
	}

	/**
	 * Internal stream function.
	 * @param soe SoundObject Entry to stream
	 * @return Number of buffers streamed and filled (usually 1).
	 */
	int SoundStream::Stream(SO_ENTRY& soe)
	{
		int streamNext = soe.next;
		SoundObject* obj = soe.obj;
		int alSource = obj->alSource;
		if(streamNext >= alStream->Size()) // is EOF?
			return 0;

		int processed; alGetSourcei(alSource, AL_BUFFERS_PROCESSED, &processed);
		if(processed) // buffers were processed
		{
			ALuint buffer; alSourceUnqueueBuffers(alSource, 1, &buffer);
			int size; alGetBufferi(buffer, AL_SIZE, &size);
			soe.base += size; // base has increased (all of this buffer has been played)

			if(buffer == alBuffer) // we can't refill alBuffer, since it's our 'first' buffer
				buffer = CreateALBuffer(STREAM_BUFFERSIZE, alStream, &streamNext); // replace the buffer with a new one
			else
				FillALBuffer(buffer, alStream, &streamNext); // fill the existing buffer
			soe.next = streamNext; // set new stream pos
			alSourceQueueBuffers(alSource, 1, &buffer); // queue the buffer
			return 1;
		}

		int state; alGetSourcei(alSource, AL_SOURCE_STATE, &state);
		if(obj->State != state)
		{
			switch(obj->State) {
			case AL_PLAYING: obj->Play(); break; // restart playing if we stopped playing for some reason..
			case AL_STOPPED: obj->Stop(); break;
			case AL_PAUSED: obj->Pause(); break;
			case AL_INITIAL: obj->Rewind(); break;
			}
		}
		return 0;
	}

	/**
	 * [internal] Load streaming data into the specified SoundObject
	 * at the optionally specified streamposition.
	 * @param so SoundObject to queue with stream data
	 * @param streampos [optional] PCM byte position in stream where to seek data from. If unspecified, stream will load data where this object's SO_ENTRY points.
	 */
	bool SoundStream::LoadStreamData(SoundObject* so, int streampos)
	{
		int index = GetIndexOf(so);
		if(index == -1) return false; // nothing to do here
		int pos = streampos == -1 ? alSources[index].next : streampos; // -1: use next, else use streampos

		int streamSize = alStream->Size() - pos; // lets calculate stream size from the SEEK position
		int numBuffers = streamSize > STREAM_BUFFERSIZE ? 2 : 1;
		alSources[index].base = pos;

		if(pos == 0) // pos 0 means we load alBuffer
		{
			alSourceQueueBuffers(so->alSource, 1, (ALuint*)&alBuffer); // queue alBuffer, which is always the 'first' buffer
			pos += numBuffers == 1 ? streamSize : STREAM_BUFFERSIZE; // gotta update pos manually
		}
		else
		{
			int buffer = CreateALBuffer(STREAM_BUFFERSIZE, alStream, &pos); // first buffer at arbitrary position
			alSourceQueueBuffers(so->alSource, 1, (ALuint*)&buffer);			
		}
		if(numBuffers == 2)
		{
			int buffer = CreateALBuffer(STREAM_BUFFERSIZE, alStream, &pos); // seek and load the 'second' buffer
			alSourceQueueBuffers(so->alSource, 1, (ALuint*)&buffer);
		}
		alSources[index].next = pos;  // pos variable is updated by CreateALBuffer
		return true;
	}

	/**
	 * [internal] Unloads all queued data for the specified SoundObject
	 * @param so SoundObject to unqueue and unload data for
	 */
	void SoundStream::ClearStreamData(SoundObject* so)
	{
		int alSource = so->alSource;
		alSourceStop(alSource); // stop the source if its playing
		int queued; alGetSourcei(alSource, AL_BUFFERS_QUEUED, &queued);

		for(int i = 0; i < queued; ++i) // unqueue all used buffers and delete them (except alBuffer!)
		{
			ALuint buffer;
			alSourceUnqueueBuffers(alSource, 1, &buffer);
			if(alBuffer == buffer)
				continue; // dont delete alBuffer!
			alDeleteBuffers(1, &buffer);
		}
	}








	static std::vector<ManagedSoundStream*> ManagedStreams;
	static HANDLE Mutex = NULL;
	static HANDLE ThreadHandle;
	static DWORD ThreadID;

	DWORD __stdcall SoundStreamManager(void* threadStart)
	{
		while(true)
		{
			WaitForSingleObject(Mutex, 100);
			for(ManagedSoundStream* stream : ManagedStreams)
				stream->Stream();
			ReleaseMutex(Mutex);

			Sleep(100); // this thread doesn't need to be very active
		}
	}

	ManagedSoundStream::ManagedSoundStream() : SoundStream()
	{
		if(!Mutex) 
		{
			Mutex = CreateMutex(0, TRUE, 0);
			ThreadHandle = CreateThread(0, 0, &SoundStreamManager, 0, 0, &ThreadID);
		}
		WaitForSingleObject(Mutex, 100);
		ManagedStreams.push_back(this);
		ReleaseMutex(Mutex);
	}

	ManagedSoundStream::~ManagedSoundStream()
	{
		WaitForSingleObject(Mutex, 100);
		// erase the stream
		ManagedStreams.erase(std::find(ManagedStreams.begin(), ManagedStreams.end(), this));
		if(ManagedStreams.size() == 0)
		{
			TerminateThread(ThreadHandle, 0);
			CloseHandle(Mutex);
			ThreadHandle = NULL;
			ThreadID = 0;
			Mutex = NULL;
		}
		ReleaseMutex(Mutex);
	}























	static inline int alSourceType(ALuint alSource) // returns the alSource type
	{
		int type; alGetSourcei(alSource, AL_SOURCE_TYPE, &type);
		return type;
	}
















	/**
	 * Creates an uninitialzed empty SoundObject
	 */
	SoundObject::SoundObject()
		: Sound(nullptr), State(AL_INITIAL)
	{
		alGenSources(1, &alSource);
	}
	/**
	 * Creates a soundobject with an attached buffer
	 * @param sound SoundBuffer this object uses for playing sounds
	 * @param loop True if sound looping is wished
	 * @param play True if sound should start playing immediatelly
	 */
	SoundObject::SoundObject(SoundBuffer* sound, bool loop, bool play)
		: Sound(nullptr), State(AL_INITIAL)
	{
		alGenSources(1, &alSource);
		if(sound) SetSound(sound, loop);
		if(play) Play();
	}

	SoundObject::~SoundObject() // unhooks any sounds and frees resources
	{
		if(Sound) SetSound(nullptr);
		alDeleteSources(1, &alSource);
		alSource = 0;
	}




	/**
	 * Sets the SoundBuffer or SoundStream for this SoundObject. Set NULL to remove and unbind the SoundBuffer.
	 * @param sound Sound to bind to this object. Can be NULL to unbind sounds from this object.
	 * @param loop [optional] Sets the sound looping or non-looping. Streams cannot be looped.
	 */
	void SoundObject::SetSound(SoundBuffer* sound, bool loop)
	{
		if(Sound) Sound->UnbindSource(this);
		if(Sound = sound) 
		{
			sound->BindSource(this);
			alSourcei(alSource, AL_LOOPING, (alSourceType(alSource) == AL_STREAMING) ? false : loop);
		}
	}

	/**
	 * @return TRUE if this SoundObject has an attached SoundStream that can be streamed.
	 */
	bool SoundObject::IsStreamable() const
	{
		return Sound ? alSourceType(alSource) == AL_STREAMING : false;
	}

	/**
	 * @return TRUE if this SoundObject is streamable and the End Of Stream was reached.
	 */
	bool SoundObject::IsEOS() const
	{
		if(!Sound || !IsStreamable()) return false; // not streamable
		return ((SoundStream*)Sound)->IsEOS(this);
	}

	/**
	 * Automatically streams new data if needed for this specific SoundObject
	 * @return Number of buffers streamed. -1 if this SoundObject is not streamable or EOS was reached.
	 */
	int SoundObject::Stream()
	{
		if(!Sound || !IsStreamable() || IsEOS()) return -1; // not streamable
		return ((SoundStream*)Sound)->Stream(this);
	}

	/**
	 * Starts playing the sound. If the sound is already playing, it is rewinded and played again from the start.
	 */
	void SoundObject::Play()
	{
		State = AL_PLAYING;
		alSourcePlay(alSource);
	}
	/**
	 * Starts playing a new sound. Any older playing sounds will be stopped and replaced with this sound.
	 * @param sound SoundBuffer or SoundStream to start playing
	 * @param loop [false] Sets if the sound is looping or not. Streams are never loopable.
	 */
	void SoundObject::Play(SoundBuffer* sound, bool loop)
	{
		SetSound(sound, loop);
		Play();
	}
	/**
	 * Stops playing the sound and unloads streaming buffers.
	 */
	void SoundObject::Stop()
	{
		State = AL_STOPPED;
		alSourceStop(alSource);
	}
	/**
	 * Temporarily pauses sound playback and doesn't unload any streaming buffers.
	 */
	void SoundObject::Pause()
	{
		State = AL_PAUSED;
		alSourcePause(alSource);
	}
	/**
	 * If current status is Playing, then this rewinds to start of the soundbuffer/stream and starts playing again.
	 * If current status is NOT playing, then any stream resources are freed and the object is reset.
	 */
	void SoundObject::Rewind()
	{
		bool wasPlaying = IsPlaying();
		State = AL_INITIAL;

		int type = alSourceType(alSource);
		if(type == AL_STATIC) // buffer
		{
			alSourceRewind(alSource);
			if(wasPlaying) Play();
		}
		else if(type == AL_STREAMING) // stream
		{
			SoundStream* strm = (SoundStream*)Sound;
		}
	}

	/**
	 * @return TRUE if the sound source is PLAYING.
	 */
	bool SoundObject::IsPlaying() const
	{
		int state; alGetSourcei(alSource, AL_SOURCE_STATE, &state);
		return state == AL_PLAYING;
	}
	/**
	 * @return TRUE if the sound source is STOPPED.
	 */
	bool SoundObject::IsStopped() const
	{
		int state; alGetSourcei(alSource, AL_SOURCE_STATE, &state);
		return state == AL_STOPPED;
	}
	/**
	 * @return TRUE if the sound source is PAUSED.
	 */
	bool SoundObject::IsPaused() const
	{
		int state; alGetSourcei(alSource, AL_SOURCE_STATE, &state);
		return state == AL_PAUSED;
	}
	/**
	 * @return TRUE if the sound source is at the beginning of the sound buffer.
	 */
	bool SoundObject::IsInitial() const
	{
		int state; alGetSourcei(alSource, AL_SOURCE_STATE, &state);
		return state == AL_INITIAL;
	}


	/**
	 * @return TRUE if the sound source is in LOOPING mode.
	 */
	bool SoundObject::IsLooping() const
	{
		int state; alGetSourcei(alSource, AL_SOURCE_STATE, &state);
		return state == AL_LOOPING;
	}
	/**
	 * Sets the looping mode of the sound source.
	 */
	void SoundObject::Looping(bool looping)
	{
		if(alSourceType(alSource) == AL_STREAMING) // buffer
			return; // cant set looping for AL_STREAMING, or it will break the logic
		alSourcei(alSource, AL_LOOPING, looping);
	}

	/**
	 * Indicates the gain (volume amplification) applied. Range [0.0..1.0] A value of 1.0 means un-attenuated/unchanged.
	 * Each division by 2 equals an attenuation of -6dB. Each multiplicaton with 2 equals an amplification of +6dB.
	 * A value of 0.0 is meaningless with respect to a logarithmic scale; it is interpreted as zero volume - the channel is effectively disabled.
	 * @param gain Gain value between 0.0..1.0
	 */
	void SoundObject::Volume(float gain)
	{
		alSourcef(alSource, AL_GAIN, gain);
	}

	/**
	 * @return Current gain value of this source
	 */
	float SoundObject::Volume() const
	{
		float gain; alGetSourcef(alSource, AL_GAIN, &gain);
		return gain;
	}

	/**
	 * Sets the pitch multiplier for this SoundObject. Always positive. Range [0.5 - 2.0] Default 1.0.
	 * @param pitch New pitch value to apply
	 */
	void SoundObject::Pitch(float pitch)
	{
		alSourcef(alSource, AL_PITCH, pitch);
	}

	/**
	 * @return Current pitch value of this SoundObject. Range [0.5 - 2.0] Default 1.0.
	 */
	float SoundObject::Pitch() const
	{
		float pitch; alGetSourcef(alSource, AL_PITCH, &pitch);
		return pitch;
	}

	/**
	 * Sets the Minimum gain value, below which the sound will never drop. Range [0.0-1.0]. Default is 0.0f.
	 * @param mingain Minimum gain value
	 */
	void SoundObject::MinGain(float mingain)
	{
		alSourcef(alSource, AL_MIN_GAIN, mingain);
	}

	/**
	 * @return Minimum possible gain of this SoundObject, below which the sound will never drop. Default is 0.0f.
	 */
	float SoundObject::MinGain() const
	{
		float mingain; alGetSourcef(alSource, AL_MIN_GAIN, &mingain);
		return mingain;
	}

	/**
	 * Sets the Maximum gain value, over which the sound will never raise. Range [0.0-1.0].
	 * @param maxgain Maximum gain value
	 */
	void SoundObject::MaxGain(float maxgain)
	{
		alSourcef(alSource, AL_MAX_GAIN, maxgain);
	}

	/**
	 * @return Maximum possible gain of this SoundObject, over which the sound will never raise. Range [0.0-1.0].
	 */
	float SoundObject::MaxGain() const
	{
		float maxgain; alGetSourcef(alSource, AL_MAX_GAIN, &maxgain);
		return maxgain;
	}

	/**
	 * @return Gets the current playback position in the SoundBuffer or SoundStream in SAMPLES
	 */
	int SoundObject::PlaybackPos() const
	{
		if(!Sound) return 0;
		if(alSourceType(alSource) == AL_STREAMING)
		{
			return ((SoundStream*)Sound)->GetSamplePos(this);
		}
		else // AL_STATIC
		{
			int offset;	alGetSourcei(alSource, AL_SAMPLE_OFFSET, &offset);
			return offset;
		}
	}

	/**
	 * Sets the playback position of the underlying SoundBuffer or SoundStream in SAMPLES
	 */
	void SoundObject::PlaybackPos(int seekpos)
	{
		if(!Sound) return;
		if(alSourceType(alSource) == AL_STREAMING)
			((SoundStream*)Sound)->Seek(this, seekpos); // seek the stream
		else // AL_STATIC
			alSourcei(alSource, AL_SAMPLE_OFFSET, seekpos);
	}

	/**
	 * @return Playback size of the underlying SoundBuffer or SoundStream in SAMPLES
	 */
	int SoundObject::PlaybackSize() const
	{
		if(!Sound) return 0;
		return Sound->Size();
	}


	/**
	 * @return Number of samples processed every second (aka SampleRate or Frequency)
	 */
	int SoundObject::SamplesPerSecond() const
	{
		if(!Sound) return 0;
		return Sound->Frequency();
	}












	/**
	 * Creates an uninitialzed Sound2D, but also
	 * setups the sound source as an environment/bakcground sound.
	 */
	Sound::Sound() : SoundObject()
	{
		Reset();
	}
	/**
	 * Creates a Sound2D with an attached buffer
	 * @param sound SoundBuffer/SoundStream this object can use for playing sounds
	 * @param loop True if sound looping is wished
	 * @param play True if sound should start playing immediatelly
	 */
	Sound::Sound(SoundBuffer* sound, bool loop, bool play) : SoundObject(sound, loop, play)
	{
		Reset();
	}

	/**
	 * Resets all background audio parameters to their defaults
	 */
	void Sound::Reset()
	{
		alSourcei(alSource, AL_SOURCE_RELATIVE, AL_TRUE); // all positions relative to the listener
		alSourcef(alSource, AL_PITCH, 1.0f); // regular pitch
		alSourcef(alSource, AL_GAIN, 1.0f); // regular waveheight
	}









	/**
	 * Creates an uninitialzed Sound3D, but also
	 * setups the sound source as a 3D positional sound.
	 */
	Sound3D::Sound3D() : SoundObject()
	{
		Reset();
	}
	/**
	 * Creates a Sound3D with an attached buffer
	 * @param sound SoundBuffer/SoundStream this object can use for playing sounds
	 * @param loop True if sound looping is wished
	 * @param play True if sound should start playing immediatelly
	 */
	Sound3D::Sound3D(SoundBuffer* sound, bool loop, bool play) : SoundObject(sound, loop, play)
	{
		Reset();
	}

	/**
	 * Resets all 3D positional audio parameters to their defaults
	 */
	void Sound3D::Reset()
	{
		alSourcef(alSource, AL_PITCH, 1.0); // regular pitch
		alSourcef(alSource, AL_GAIN, 1.0); // regular waveheight
		
		ALfloat v[] = { 0.0f, 0.0f, 0.0f };
		alSourcefv(alSource, AL_POSITION, v);
		alSourcefv(alSource, AL_VELOCITY, v);
		alSourcefv(alSource, AL_DIRECTION, v);
	}

	/**
	 * Sets the 3D position of this Sound3D object
	 * @param xyz Float vector containing 3D position X Y Z components
	 */
	void Sound3D::Position(float* xyz)
	{
		alSourcefv(alSource, AL_POSITION, xyz);
	}

	/**
	 * @return 3D position vector of this SoundObject
	 */
	Vector3 Sound3D::Position() const
	{
		float xyz[3];
		alGetSourcefv(alSource, AL_POSITION, xyz);
		return Vector3(xyz[0], xyz[1], xyz[2]);
	}

	/**
	 * Sets the 3D direction of this Sound3D object
	 * @param xyz Float vector containing 3D direction X Y Z components
	 */
	void Sound3D::Direction(float* xyz)
	{
		alSourcefv(alSource, AL_DIRECTION, xyz);
	}

	/**
	 * @return 3D direction vector of this SoundObject
	 */
	Vector3 Sound3D::Direction() const
	{
		float xyz[3];
		alGetSourcefv(alSource, AL_DIRECTION, xyz);
		return Vector3(xyz[0], xyz[1], xyz[2]);
	}

	/**
	 * Sets the 3D velocity of this Sound3D object
	 * @param xyz Float vector containing 3D velocity X Y Z components
	 */
	void Sound3D::Velocity(float* xyz)
	{
		alSourcefv(alSource, AL_VELOCITY, xyz);
	}

	/**
	 * @return 3D velocity vector of this SoundObject
	 */
	Vector3 Sound3D::Velocity() const
	{
		float xyz[3];
		alGetSourcefv(alSource, AL_VELOCITY, xyz);
		return Vector3(xyz[0], xyz[1], xyz[2]);
	}

	/**
	 * Sets the position of this SoundObject as relative to the global listener
	 * @param isrelative TRUE if the position of this SoundObject is relative
	 */
	void Sound3D::Relative(bool isrelative)
	{
		alSourcei(alSource, AL_SOURCE_RELATIVE, isrelative);
	}

	/**
	 * @return TRUE if the position of this SoundObject is relative
	 */
	bool Sound3D::IsRelative() const
	{
		int value; alGetSourcei(alSource, AL_SOURCE_RELATIVE, &value);
		return value ? true : false;
	}


	void Sound3D::MaxDistance(float maxdist)
	{
		alSourcef(alSource, AL_MAX_DISTANCE, maxdist);
	}
	float Sound3D::MaxDistance() const
	{
		float value; alGetSourcef(alSource, AL_MAX_DISTANCE, &value);
		return value;
	}

	void Sound3D::RolloffFactor(float rolloff)
	{
		alSourcef(alSource, AL_ROLLOFF_FACTOR, rolloff);
	}
	float Sound3D::RolloffFactor() const
	{
		float value; alGetSourcef(alSource, AL_ROLLOFF_FACTOR, &value);
		return value;
	}

	void Sound3D::ReferenceDistance(float refdist)
	{
		alSourcef(alSource, AL_REFERENCE_DISTANCE, refdist);
	}
	float Sound3D::ReferenceDistance() const
	{
		float value; alGetSourcef(alSource, AL_REFERENCE_DISTANCE, &value);
		return value;
	}

	void Sound3D::ConeOuterGain(float value)
	{
		alSourcef(alSource, AL_CONE_OUTER_GAIN, value);
	}
	float Sound3D::ConeOuterGain() const
	{
		float value; alGetSourcef(alSource, AL_CONE_OUTER_GAIN, &value);
		return value;
	}

	void Sound3D::ConeInnerAngle(float angle)
	{
		alSourcef(alSource, AL_CONE_INNER_ANGLE, angle);
	}
	float Sound3D::ConeInnerAngle() const
	{
		float value; alGetSourcef(alSource, AL_CONE_INNER_ANGLE, &value);
		return value;
	}

	/**
	 * Sets the outer sound cone angle in degrees. Default is 360.
	 * Outer cone angle cannot be smaller than inner cone angle
	 * @param angle Angle 
	 */
	void Sound3D::ConeOuterAngle(float angle)
	{
		float innerAngle = ConeInnerAngle();
		if(angle < innerAngle)
			angle = innerAngle;
		alSourcef(alSource, AL_CONE_OUTER_ANGLE, angle);
	}
	float Sound3D::ConeOuterAngle() const
	{
		float value; alGetSourcef(alSource, AL_CONE_OUTER_ANGLE, &value);
		return value;
	}












	/**
	 * Sets the master gain value of the listener object for Audio3D
	 * @param gain Gain value to set for the listener. Range[0.0-2.0].
	 */
	void Listener::Gain(float gain)
	{
		alListenerf(AL_GAIN, gain);
	}

	/**
	 * Gets the master gain value of the listener object for Audio3D
	 * @return Master gain value. Range[0.0-2.0].
	 */
	float Listener::Gain()
	{
		float value; alGetListenerf(AL_GAIN, &value);
		return value;
	}

	/**
	 * Sets the position of the listener object for Audio3D
	 * @param xyz Float array containing x y z components of the position
	 */
	void Listener::Position(float* xyz)
	{
		alListenerfv(AL_POSITION, xyz);
	}

	/**
	 * @return Position of the listener object for Audio3D
	 */
	Vector3 Listener::Position()
	{
		float xyz[3];
		alGetListenerf(AL_POSITION, xyz);
		return Vector3(xyz[0], xyz[1], xyz[2]);
	}

	/**
	 * Sets the velocity of the listener object for Audio3D
	 * @param xyz Float array containing x y z components of the velocity
	 */
	void Listener::Velocity(float* xyz)
	{
		alListenerfv(AL_VELOCITY, xyz);
	}

	/**
	 * @return Velocity of the listener object for Audio3D
	 */
	Vector3 Listener::Velocity()
	{
		float xyz[3];
		alGetListenerf(AL_VELOCITY, xyz);
		return Vector3(xyz[0], xyz[1], xyz[2]);
	}

	/**
	 * Sets the direction of the listener object for Audio3D
	 * @param target Vector containing x y z components of target where to 'look', thus changing the orientation.
	 * @param up Vector containing x y z components of the orientation 'up' vector
	 */
	void Listener::LookAt(Vector3& target, Vector3& up)
	{
		LookAt(target.x, target.y, target.z, up.x, up.y, up.z);
	}

	/**
	 * Sets the direction of the listener object for Audio3D
	 * @param xyzATxyzUP Float array containing x y z components of the direction
	 */
	void Listener::LookAt(float* xyzATxyzUP)
	{
		alListenerfv(AL_ORIENTATION, xyzATxyzUP);
	}

	/**
	 * @return Target position of the listener object's orientation
	 */
	Vector3 Listener::Target()
	{
		float xyzATxyzUP[6];
		alGetListenerfv(AL_ORIENTATION, xyzATxyzUP);
		return Vector3(xyzATxyzUP[0], xyzATxyzUP[1], xyzATxyzUP[2]);
	}

	/**
	 * @return Up vector of the listener object's orientation
	 */
	Vector3 Listener::Up()
	{
		float xyzATxyzUP[6];
		alGetListenerfv(AL_ORIENTATION, xyzATxyzUP);
		return Vector3(xyzATxyzUP[3], xyzATxyzUP[4], xyzATxyzUP[5]);
	}


} // namespace S3D
