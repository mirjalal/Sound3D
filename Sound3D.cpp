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
#include <Windows.h>

#pragma comment(lib, "XAudio2_7/X3DAudio.lib")

#ifdef _DEBUG
#define indebug(x) x
#else
#define indebug(x) // ...
#endif

namespace S3D 
{

	static IXAudio2* xEngine;					// the core engine for XAudio2
	static IXAudio2MasteringVoice* xMaster;		// the Mastering voice is the global LISTENER / mixer
	static X3DAUDIO_HANDLE x3DAudioHandle;		// X3DSound
	static X3DAUDIO_LISTENER xListener;			// global listener position for X3DSound


	static void UninitXAudio2()
	{
		xMaster->DestroyVoice();
		xEngine->Release();
	}
	static void InitXAudio2()
	{
		CoInitializeEx(NULL, COINIT_MULTITHREADED);
		XAudio2Create(&xEngine);
		xEngine->CreateMasteringVoice(&xMaster);
		X3DAudioInitialize(SPEAKER_STEREO, 340.29f, x3DAudioHandle);

		atexit(UninitXAudio2);
	}


	/**
	 * @param ctx SoundBuffer passed to the buffer as its Context
	 * @param size Size of the buffer to create and fill with audio data
	 * @param strm AudioStream to stream from
	 * @param pos [optional] Position of the stream to stream from. Returns the new position of the stream. (in BYTES)
	 * @return NEW buffer if successful. NULL if EndOfStream or OutOfMemory.
	 */
	static XABuffer* CreateXABuffer(SoundBuffer* ctx, int size, AudioStreamer* strm, int* pos = nullptr)
	{
		if (pos) strm->Seek(*pos); // seek to specified pos, let the AudioStream handle error conditions
		if (strm->IsEOS()) return nullptr; // EOS(), failed!

		// min(size, available);
		int bytesToRead = strm->Available();
		if (size < bytesToRead) bytesToRead = size;

		XABuffer* buffer = (XABuffer*)malloc(sizeof(XABuffer)+bytesToRead);
		if (!buffer) return nullptr; // out of memory :S
		BYTE* data = (BYTE*)buffer + sizeof(XABuffer); // sound data follows after the XABuffer header

		int bytesRead = strm->ReadSome(data, bytesToRead);
		if (pos) *pos += bytesRead; // update position

		buffer->Flags = strm->IsEOS() ? XAUDIO2_END_OF_STREAM : 0; // end of stream?
		buffer->AudioBytes = bytesRead;
		buffer->pAudioData = data;
		buffer->PlayBegin = 0;		// first sample to play
		buffer->PlayLength = 0;		// number of samples to play
		buffer->LoopBegin = 0;		// first sample to loop
		buffer->LoopLength = 0;		// number of samples to loop
		buffer->LoopCount = 0;		// how many times to loop the region
		buffer->pContext = ctx;		// context of the buffer
		indebug(ctx->RefCount()); // access the buffer Context in debug mode, to hopefully catch invalid ctx's
		int sampleSize = strm->SingleSampleSize();
		buffer->nBytesPerSample = sampleSize;

		WAVEFORMATEX& wf = buffer->wf;
		wf.wFormatTag = WAVE_FORMAT_PCM;
		wf.nChannels = strm->Channels();
		buffer->nPCMSamples = bytesRead / (sampleSize * wf.nChannels);
		wf.nSamplesPerSec = strm->Frequency();
		wf.wBitsPerSample = sampleSize * 8;
		wf.nBlockAlign = (wf.nChannels * sampleSize);
		wf.nAvgBytesPerSec = wf.nBlockAlign * wf.nSamplesPerSec;
		wf.cbSize = sizeof(WAVEFORMATEX);
		
		// this is enough to create an somewhat unique pseudo-hash:
		buffer->wfHash = wf.nSamplesPerSec + (wf.nChannels * 25) + (wf.wBitsPerSample * 7);
		return buffer;
	}

	/**
	 * Streams next chunk from the Stream set in XABuffer. Size is determined by existing buffer size.
	 * @param buffer Buffer to fill with audio data
	 * @param pos [optional] Position of the stream to stream from (in BYTES)
	 */
	static void StreamXABuffer(XABuffer* buffer, AudioStreamer* strm, int* pos = nullptr)
	{
		if (pos) strm->Seek(*pos); // seek to specified pos, let the AudioStream handle error conditions
		
		buffer->AudioBytes = strm->ReadSome((void*)buffer->pAudioData, buffer->AudioBytes);
		if (strm->IsEOS()) // end of stream was reached
			buffer->Flags = XAUDIO2_END_OF_STREAM;

		if (pos) *pos += buffer->AudioBytes; // update position
	}

	/**
	 * @param buffer Reference to an Audio buffer to destroy. Buffer will be NULL after this call.
	 */
	static void DestroyXABuffer(XABuffer*& buffer)
	{
		free((void*)buffer);
		buffer = nullptr;
	}

	static int GetBuffersQueued(IXAudio2SourceVoice* source)
	{
		XAUDIO2_VOICE_STATE state;
		source->GetState(&state);
		return state.BuffersQueued;
	}






	/**
	 * Creates a new SoundBuffer object
	 */
	SoundBuffer::SoundBuffer() : refCount(0), xaBuffer(nullptr)
	{
		if (!xEngine) InitXAudio2();
	}

	/**
	 * Creates a new SoundBuffer and loads the specified sound file
	 * @param file Path to sound file to load
	 */
	SoundBuffer::SoundBuffer(const char* file) : refCount(0), xaBuffer(nullptr)
	{
		if (!xEngine) InitXAudio2();
		Load(file);
	}

	/**
	 * Destroyes and unloads this buffer
	 */
	SoundBuffer::~SoundBuffer()
	{
		if (xaBuffer)
			Unload();
	}

	/**
	 * @return Frequency in Hz of this SoundBuffer data
	 */
	int SoundBuffer::Frequency() const
	{
		return xaBuffer->wf.nSamplesPerSec;
	}

	/**
	 * @return Number of bits in a sample of this SoundBuffer data (8 or 16)
	 */
	int SoundBuffer::SampleBits() const
	{
		return xaBuffer->wf.wBitsPerSample;
	}

	/**
	 * @return Number of bytes in a sample of this SoundBuffer data (1 or 2)
	 */
	int SoundBuffer::SampleBytes() const
	{
		return xaBuffer->nBytesPerSample;
	}

	/**
	 * @return Number of sound channels in the SoundBuffer data (1 or 2)
	 */
	int SoundBuffer::Channels() const
	{
		return xaBuffer->wf.nChannels;
	}

	/** 
	 * @return Size of a full sample block in bytes [LL][RR] (1 to 4)
	 */
	int SoundBuffer::FullSampleSize() const 
	{
		return xaBuffer->wf.nBlockAlign;
	}

	/**
	 * @return Size of this SoundBuffer or SoundStream in BYTES
	 */
	int SoundBuffer::SizeBytes() const
	{
		return xaBuffer->AudioBytes;
	}

	/**
	 * @return Number of Bytes Per Second for this audio data (Frequency * Channels * SampleBytes)
	 */
	int SoundBuffer::BytesPerSecond() const
	{
		return xaBuffer->wf.nAvgBytesPerSec;
	}

	/**
	 * @return Size of this SoundBuffer in PCM SAMPLES
	 */
	int SoundBuffer::Size() const
	{
		return xaBuffer->nPCMSamples;
	}

	/**
	 * @return Number of SoundObjects that still reference this SoundBuffer.
	 */
	int SoundBuffer::RefCount() const 
	{
		return refCount; 
	}

	/**
	 * @return TRUE if this object is a Sound stream
	 */
	bool SoundBuffer::IsStream() const 
	{ 
		return false; 
	}

	/**
	 * @return Wave format descriptor for this buffer
	 */
	const WAVEFORMATEX* SoundBuffer::WaveFormat() const
	{
		return xaBuffer ? &xaBuffer->wf : nullptr;
	}

	/**
	 * @return Unique hash to distinguish between different Wave formats
	 */
	unsigned SoundBuffer::WaveFormatHash() const
	{
		return xaBuffer ? xaBuffer->wfHash : 0;
	}

	/**
	 * Loads this SoundBuffer with data found in the specified file.
	 * Supported formats: .wav .mp3
	 * @param file Sound file to load
	 * @return TRUE if loading succeeded and a valid buffer was created.
	 */
	bool SoundBuffer::Load(const char* file)
	{
		if (xaBuffer) // is there existing data?
			return false;
		
		AudioStreamer mem; // temporary stream
		AudioStreamer* strm = &mem;
		if (!CreateAudioStreamer(strm, file))
			return false; // invalid file format or file not found

		if (!strm->OpenStream(file))
			return false; // failed to open the stream (probably not really correct format)

		xaBuffer = CreateXABuffer(this, strm->Size(), strm);
		strm->CloseStream(); // close this manually, otherwise we get a nasty error when the dtor runs...
		return xaBuffer != nullptr;
	}

	/**
	 * Tries to release the underlying sound buffer and free the memory.
	 * @note This function will fail if refCount > 0. This means there are SoundObjects still using this SoundBuffer
	 * @return TRUE if SoundBuffer was freed, FALSE if SoundBuffer is still used by a SoundObject.
	 */
	bool SoundBuffer::Unload()
	{
		if (!xaBuffer)
			return true; // yes, its unloaded
		if (refCount > 0) {
			indebug(printf("SoundBuffer::Unload() Memory Leak: failed to delete alBuffer, because it's still in use.\n"));
			return false; // can't do anything here while still referenced
		}
		DestroyXABuffer(xaBuffer);
		return true;
	}

	/**
	 * Binds a specific source to this SoundBuffer and increases the refCount.
	 * @param so SoundObject to bind to this SoundBuffer.
	 * @return FALSE if the binding failed. TODO: POSSIBLE REASONS?
	 */
	bool SoundBuffer::BindSource(SoundObject* so)
	{
		if (!xaBuffer) 
			return false; // no data

		if (so->Sound == this)
			return false; // no double-binding dude, it will mess up refCounting.

		so->Source->SubmitSourceBuffer(xaBuffer); // enqueue this buffer
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
		if (so->Sound == this) // correct buffer link?
		{
			so->Source->Stop(); // make sure its stopped (otherwise Flush won't work)
			if (GetBuffersQueued(so->Source))
				so->Source->FlushSourceBuffers(); // ensure not in queue anymore
			--refCount;
		}
		return true;
	}

	/**
	 * Resets the buffer in the context of the specified SoundObject
	 * @note Calls ResetStream on AudioStreams.
	 * @param so SoundObject to reset this buffer for
	 * @return TRUE if reset was successful
	 */
	bool SoundBuffer::ResetBuffer(SoundObject* so)
	{
		if (!xaBuffer || !so->Source)
			return false; // nothing to do here

		so->Source->Stop();
		if (GetBuffersQueued(so->Source)) // only flush IF we have buffers to flush
			so->Source->FlushSourceBuffers();

		so->Source->SubmitSourceBuffer(xaBuffer);
		return true;
	}









	/**
	 * Creates a new SoundsStream object
	 */
	SoundStream::SoundStream()
	{
	}

	/**
	 * Creates a new SoundStream object and loads the specified sound file
	 * @param file Path to the sound file to load
	 */
	SoundStream::SoundStream(const char* file)
	{
		Load(file);
	}

	/**
	 * Destroys and unloads any resources held
	 */
	SoundStream::~SoundStream()
	{
		if (xaBuffer) // active buffer?
			Unload();
	}

	/**
	 * @return Size of this SoundStream in PCM SAMPLES
	 */
	int SoundStream::Size() const
	{
		return alStream ? (alStream->Size() / FullSampleSize()) : 0;
	}

	/**
	 * @return TRUE if this object is a Sound stream
	 */
	bool SoundStream::IsStream() const
	{
		return true;
	}

	/**
	 * Initializes this SoundStream with data found in the specified file.
	 * Supported formats: .wav .mp3
	 * @param file Sound file to load
	 * @return TRUE if loading succeeded and a stream was initialized.
	 */
	bool SoundStream::Load(const char* file)
	{
		if (xaBuffer) // is there existing data?
			return false;

		if (!(alStream = CreateAudioStreamer(file)))
			return false; // :(
		
		if (!alStream->OpenStream(file))
			return false;

		// load the first buffer in the stream:
		xaBuffer = CreateXABuffer(this, alStream->BytesPerSecond(), alStream, 0);
		return xaBuffer != nullptr;
	}

	/**
	 * Tries to release the underlying sound buffers and free the memory.
	 * @note This function will fail if refCount > 0. This means there are SoundObjects still using this SoundStream
	 * @return TRUE if SoundStream data was freed, FALSE if SoundStream is still used by a SoundObject.
	 */
	bool SoundStream::Unload()
	{
		if (!xaBuffer)
			return true; // yes, its unloaded
		if (refCount > 0) {
			indebug(printf("SoundStream::Unload() Memory Leak: failed to delete xaBuffer, because it's still in use.\n"));
			return false; // can't do anything here while still referenced
		}
		DestroyXABuffer(xaBuffer);
		if (alStream) { delete alStream; alStream = NULL; }
		return true;
	}

	/**
	 * Binds a specific source to this SoundStream and increases the refCount.
	 * @param so SoundObject to bind to this SoundStream.
	 * @return FALSE is the binding failed. TODO: POSSIBLE REASONS?
	 */
	bool SoundStream::BindSource(SoundObject* so)
	{
		if (!xaBuffer) 
			return false; // no data loaded yet

		alSources.emplace_back(so);				// default streamPos
		LoadStreamData(alSources.back(), 0);	// load initial stream data (2 buffers)

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
		if (!xaBuffer)
			return false; // no data loaded yet
		
		SO_ENTRY* e = GetSOEntry(so);
		if (!e) return false; // source doesn't exist

		ClearStreamData(*e); // unload all buffers

		alSources.erase(alSources.begin() + (e - alSources.data()));
		--refCount;
		return true;
	}

	/**
	 * Resets the buffer in the context of the specified SoundObject
	 * @note Calls ResetStream on AudioStreams.
	 * @param so SoundObject to reset this buffer for
	 * @return TRUE if reset was successful
	 */
	bool SoundStream::ResetBuffer(SoundObject* so)
	{
		return ResetStream(so);
	}

	/**
	 * Streams the next Buffer block from the stream.
	 * @param so Specific SoundObject to stream with.
	 * @return TRUE if a buffer was streamed. FALSE if EOS() or stream is busy.
	 */
	bool SoundStream::StreamNext(SoundObject* so)
	{
		if (!xaBuffer) 
			return false; // nothing to do here
		if (SO_ENTRY* e = GetSOEntry(so))
		{
			if (e->busy) return false; // can't stream when stream is busy
			return StreamNext(*e);
		}
		return false; // nothing to stream
	}

	/**
	 * Resets the stream by unloading previous buffers and requeuing the first two buffers.
	 * @param so SoundObject to reset the stream for
	 * @return TRUE if stream was successfully reloaded
	 */
	bool SoundStream::ResetStream(SoundObject* so)
	{
		// simply clear and load again, to avoid code maintenance hell
		if (SO_ENTRY* e = GetSOEntry(so)) {
			ClearStreamData(*e);
			return LoadStreamData(*e, 0);
		}
		return false;
	}

	/**
	 * @param so Specific SoundObject to check for end of stream
	 * @return TRUE if End of Stream was reached or if there is no stream loaded
	 */
	bool SoundStream::IsEOS(const SoundObject* so) const
	{
		SO_ENTRY* e = GetSOEntry(so);
		return e ? true : (alStream ? (e->next >= alStream->Size()) : true);
	}

	/**
	 * @param so SoundObject to query index of
	 * @return A valid pointer if this SoundObject is bound. Otherwise NULL.
	 */
	SoundStream::SO_ENTRY* SoundStream::GetSOEntry(const SoundObject* so) const
	{
		for (const SO_ENTRY& e : alSources)
			if (e.obj == so)
				return (SO_ENTRY*)&e;
		return nullptr; // not found
	}

	/**
	 * Seeks to the specified sample position in the stream.
	 * @note The SoundObject will stop playing and must be manually restarted!
	 * @param so SoundObject to perform seek on
	 * @param samplepos Position in the stream in samples [0..SoundStream::Size()]
	 */
	void SoundStream::Seek(SoundObject* so, int samplepos)
	{
		if (SO_ENTRY* e = GetSOEntry(so))
		{
			// if samplepos out of bounds THEN 0 ELSE convert samplepos to bytepos
			int bytepos = (samplepos >= Size()) ? 0 : samplepos * xaBuffer->wf.nBlockAlign;
			ClearStreamData(*e);
			LoadStreamData(*e, bytepos);

		}

	}

	//// PROTECTED: ////

	/**
	 * Internal stream function.
	 * @param soe SoundObject Entry to stream
	 * @return TRUE if a buffer was streamed
	 */
	bool SoundStream::StreamNext(SO_ENTRY& e)
	{
		if (e.next >= alStream->Size()) // is EOF?
			return false;

		// front buffer was processed, swap buffers:
		std::swap(e.front, e.back);

		e.base = e.next; // shift the base pointer forward
		if (e.back == xaBuffer) // we can't refill xaBuffer
		{
			e.back = CreateXABuffer(this, xaBuffer->wf.nAvgBytesPerSec, alStream, &e.next);
			if (!e.back)
				return false; // oh no...
		}
		else
		{
			if (!e.back) // no backbuffer. probably ClearStreamData() was called
				return false;
			StreamXABuffer(e.back, alStream, &e.next);
		}

		e.obj->Source->SubmitSourceBuffer(e.back); // submit the backbuffer to the queue
		return true;
	}

	/**
	 * [internal] Load streaming data into the specified SoundObject
	 * at the optionally specified streamposition.
	 * @param so SoundObject to queue with stream data
	 * @param streampos [optional] PCM byte position in stream where to seek data from. 
	 *                  If unspecified (default -1), stream will use the current streampos
	 */
	bool SoundStream::LoadStreamData(SO_ENTRY& so, int streampos)
	{
		int pos = streampos == -1 ? so.next : streampos; // -1: use next, else use streampos
		int streamSize = alStream->Size() - pos; // lets calculate stream size from the SEEK position
		int bytesPerSecond = alStream->BytesPerSecond();
		int numBuffers = streamSize > bytesPerSecond ? 2 : 1;
		IXAudio2SourceVoice* source = so.obj->Source;

		so.base = pos;
		if (pos == 0) // pos 0 means we load alBuffer
		{
			pos += xaBuffer->AudioBytes; // update pos
			source->SubmitSourceBuffer(so.front = xaBuffer);
		}
		else // load at arbitrary position
		{
			so.front = CreateXABuffer(this, bytesPerSecond, alStream, &pos);
			source->SubmitSourceBuffer(so.front);
		}

		if (numBuffers == 2) // also load a backbuffer
		{
			so.back = CreateXABuffer(this, bytesPerSecond, alStream, &pos); 
			source->SubmitSourceBuffer(so.back);
		}
		so.next = pos;  // pos variable was updated by CreateXABuffer
		return true;
	}

	/**
	 * [internal] Unloads all queued data for the specified SoundObject
	 * @param so SoundObject to unqueue and unload data for
	 */
	void SoundStream::ClearStreamData(SO_ENTRY& so)
	{
		so.busy = TRUE;
		IXAudio2SourceVoice* source = so.obj->Source;
		source->Stop();
		if (GetBuffersQueued(source)) // only flush if we have something to flush
			source->FlushSourceBuffers();

		if (so.front) {
			if (so.front != xaBuffer)
				delete so.front;
			so.front = nullptr;
		}
		if (so.back)
			delete so.back, so.back = nullptr;
		so.busy = FALSE;
	}















	struct SoundObjectState : public IXAudio2VoiceCallback
	{
		SoundObject* sound;
		bool isInitial;		// is the Sound object Rewinded to its initial position?
		bool isPlaying;		// is the Voice digesting buffers?
		bool isLoopable;	// should this sound act as a loopable sound?
		bool isPaused;		// currently paused?
		XABuffer shallow;	// a shallow buffer reference (no actual data)

		SoundObjectState(SoundObject* so) 
			: sound(so), 
			isInitial(false), isPlaying(false), 
			isLoopable(false), isPaused(false)
		{
		}

		// end of stream was reached (last buffer object was processed)
		void __stdcall OnStreamEnd() override
		{
			if (isLoopable) // loopable?
				sound->Rewind(); // rewind the sound and continue playing
			else
				isPlaying = false;
		}

		// a buffer object finished processing
		void __stdcall OnBufferEnd(void* ctx) override
		{
			isInitial = false;
			if (((SoundBuffer*)ctx)->IsStream())
			{
				// stream fetch next buffer for this sound
				((SoundStream*)ctx)->StreamNext(sound);
			}
		}

		void __stdcall OnVoiceProcessingPassStart(UINT32 samplesRequired) override {}
		void __stdcall OnVoiceProcessingPassEnd() override {}
		void __stdcall OnBufferStart(void* ctx) override {}
		void __stdcall OnLoopEnd(void* ctx) override {}
		void __stdcall OnVoiceError(void* ctx, HRESULT error) override {}

	};






	/**
	 * Creates an uninitialzed empty SoundObject
	 */
	SoundObject::SoundObject()
		: Sound(nullptr), Source(nullptr), State(nullptr)
	{
		memset(&Emitter, 0, sizeof(Emitter));
		Emitter.ChannelCount = 1;
		Emitter.CurveDistanceScaler = FLT_MIN;
	}
	/**
	 * Creates a soundobject with an attached buffer
	 * @param sound SoundBuffer this object uses for playing sounds
	 * @param loop True if sound looping is wished
	 * @param play True if sound should start playing immediatelly
	 */
	SoundObject::SoundObject(SoundBuffer* sound, bool loop, bool play)
		: Sound(nullptr), Source(nullptr), State(nullptr)
	{
		memset(&Emitter, 0, sizeof(Emitter));
		Emitter.ChannelCount = 1;
		Emitter.CurveDistanceScaler = FLT_MIN;

		if (sound) SetSound(sound, loop);
		if (play) Play();
	}

	SoundObject::~SoundObject() // unhooks any sounds and frees resources
	{
		if (Sound) SetSound(nullptr);
		if (Source) Source->DestroyVoice(), Source = nullptr;
	}




	/**
	 * Sets the SoundBuffer or SoundStream for this SoundObject. Set NULL to remove and unbind the SoundBuffer.
	 * @param sound Sound to bind to this object. Can be NULL to unbind sounds from this object.
	 * @param loop [optional] Sets the sound looping or non-looping. Streams cannot be looped.
	 */
	void SoundObject::SetSound(SoundBuffer* sound, bool loop)
	{
		if (Sound) Sound->UnbindSource(this); // unbind old, but still keep it around
		if (sound) // new sound?
		{
			if (!Source) // no Source object created yet? First init.
			{
				State = new SoundObjectState(this);
				xEngine->CreateSourceVoice(&Source, sound->WaveFormat(), 0, 2.0F, State);
			}
			else if (sound->WaveFormatHash() != Sound->WaveFormatHash()) // WaveFormat has changed?
			{
				Source->DestroyVoice(); // Destroy old and re-create with new
				xEngine->CreateSourceVoice(&Source, sound->WaveFormat(), 0, 2.0F, State);
			}
			sound->BindSource(this);
			State->isInitial = true;
			State->isPlaying = false;
			State->isLoopable = loop;
			State->isPaused = false;
			Sound = sound; // set new Sound
		}
	}

	/**
	 * @return TRUE if this SoundObject has an attached SoundStream that can be streamed.
	 */
	bool SoundObject::IsStreamable() const
	{
		return Sound && Sound->IsStream();
	}

	/**
	 * @return TRUE if this SoundObject is streamable and the End Of Stream was reached.
	 */
	bool SoundObject::IsEOS() const
	{
		return Sound && Sound->IsStream() && ((SoundStream*)Sound)->IsEOS(this);
	}


	/**
	 * Starts playing the sound. If the sound is already playing, it is rewinded and played again from the start.
	 */
	void SoundObject::Play()
	{
		if (State->isPlaying) Rewind();		// rewind to start of stream and continue playing
		else if (Source)
		{
			State->isPlaying = true;
			State->isPaused = false;
			if (!GetBuffersQueued(Source))		// no buffers queued (track probably finished)
			{
				State->isInitial = true;
				Sound->ResetBuffer(this);	// reset buffer to beginning
			}
			Source->Start();				// continue if paused or suspended
		}
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
		if (Source && State->isPlaying) { // only if isPlaying, to avoid rewind
			State->isPlaying = false;
			State->isPaused = false;
			Source->Stop();
			Source->FlushSourceBuffers();
		}
	}
	/**
	 * Temporarily pauses sound playback and doesn't unload any streaming buffers.
	 */
	void SoundObject::Pause()
	{
		if (Source)
		{
			State->isPlaying = false;
			State->isPaused = true;
			Source->Stop(); // Stop() effectively pauses playback
		}
	}
	/**
	 * If current status is Playing, then this rewinds to start of the soundbuffer/stream and starts playing again.
	 * If current status is NOT playing, then any stream resources are freed and the object is reset.
	 */
	void SoundObject::Rewind()
	{
		Sound->ResetBuffer(this); // reset stream or buffer to initial state
		State->isInitial = true;
		State->isPaused = false;
		if (State->isPlaying) // should we continue playing?
		{
			Source->Start();
		}
	}

	/**
	 * @return TRUE if the sound source is PLAYING.
	 */
	bool SoundObject::IsPlaying() const
	{
		return State && State->isPlaying;
	}

	/**
	 * @return TRUE if the sound source is STOPPED.
	 */
	bool SoundObject::IsStopped() const
	{
		return !State || !State->isPlaying;
	}

	/**
	 * @return TRUE if the sound source is PAUSED.
	 */
	bool SoundObject::IsPaused() const
	{
		return State && State->isPaused;
	}

	/**
	 * @return TRUE if the sound source is at the beginning of the sound buffer.
	 */
	bool SoundObject::IsInitial() const
	{
		return State && State->isInitial;
	}


	/**
	 * @return TRUE if the sound source is in LOOPING mode.
	 */
	bool SoundObject::IsLooping() const
	{
		return State && State->isLoopable;
	}

	/**
	 * Sets the looping mode of the sound source.
	 */
	void SoundObject::Looping(bool looping)
	{
		if (State) State->isLoopable = looping;
	}

	/**
	 * Indicates the gain (volume amplification) applied. Range [0.0f .. 1.0f]
	 * Each division by 2 equals an attenuation of -6dB. Each multiplicaton with 2 equals an amplification of +6dB.
	 * A value of 0.0 is meaningless with respect to a logarithmic scale; it is interpreted as zero volume - the channel is effectively disabled.
	 * @param gain Gain value between 0.0..1.0
	 */
	void SoundObject::Volume(float volume)
	{
		Source->SetVolume(volume);
	}

	/**
	 * @return Current gain value of this source
	 */
	float SoundObject::Volume() const
	{
		float volume;
		Source->GetVolume(&volume);
		return volume;
	}

	/**
	 * @return Gets the current playback position in the SoundBuffer or SoundStream in SAMPLES
	 */
	int SoundObject::PlaybackPos() const
	{
		if (!Source) return 0;
		XAUDIO2_VOICE_STATE state;
		Source->GetState(&state);
		return (int)state.SamplesPlayed;
	}

	/**
	 * Sets the playback position of the underlying SoundBuffer or SoundStream in SAMPLES
	 */
	void SoundObject::PlaybackPos(int seekpos)
	{
		if (!Sound) return;
		if (Sound->IsStream()) // stream objects
		{
			((SoundStream*)Sound)->Seek(this, seekpos); // seek the stream
		}
		else // single buffer objects
		{
			// first create a shallow copy of the xaBuffer:
			XABuffer& shallow = State->shallow = *Sound->xaBuffer;
			shallow.PlayBegin = seekpos;
			shallow.PlayLength = shallow.nPCMSamples - seekpos;

			State->isPaused = false;
			Source->Stop();
			if (GetBuffersQueued(Source)) // only flush if there is something to flush
				Source->FlushSourceBuffers();
			Source->SubmitSourceBuffer(&shallow);
		}
		if (State->isPlaying) 
			Source->Start();
	}

	/**
	 * @return Playback size of the underlying SoundBuffer or SoundStream in SAMPLES
	 */
	int SoundObject::PlaybackSize() const
	{
		return Sound ? Sound->Size() : 0;
	}


	/**
	 * @return Number of samples processed every second (aka SampleRate or Frequency)
	 */
	int SoundObject::SamplesPerSecond() const
	{
		return Sound ? Sound->Frequency() : 0;
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

	}

	/**
	 * Sets the 3D position of this Sound3D object
	 * @param x Position x component
	 * @param y Position Y component
	 * @param z Position z component
	 */
	void Sound3D::Position(float x, float y, float z)
	{

	}

	/**
	 * Sets the 3D position of this Sound3D object
	 * @param xyz Float vector containing 3D position X Y Z components
	 */
	void Sound3D::Position(float* xyz)
	{
	}

	/**
	 * @return 3D position vector of this SoundObject
	 */
	Vector3 Sound3D::Position() const
	{
		float xyz[3];
		return Vector3(xyz[0], xyz[1], xyz[2]);
	}

	/**
	 * Sets the 3D direction of this Sound3D object
	 * @param x Direction x component
	 * @param y Direction Y component
	 * @param z Direction z component
	 */
	void Sound3D::Direction(float x, float y, float z)
	{

	}

	/**
	 * Sets the 3D direction of this Sound3D object
	 * @param xyz Float vector containing 3D direction X Y Z components
	 */
	void Sound3D::Direction(float* xyz)
	{
	}

	/**
	 * @return 3D direction vector of this SoundObject
	 */
	Vector3 Sound3D::Direction() const
	{
		float xyz[3];
		return Vector3(xyz[0], xyz[1], xyz[2]);
	}

	/**
	 * Sets the 3D velocity of this Sound3D object
	 * @param x Velocity x component
	 * @param y Velocity Y component
	 * @param z Velocity z component
	 */
	void Sound3D::Velocity(float x, float y, float z)
	{

	}

	/**
	 * Sets the 3D velocity of this Sound3D object
	 * @param xyz Float vector containing 3D velocity X Y Z components
	 */
	void Sound3D::Velocity(float* xyz)
	{
	}

	/**
	 * @return 3D velocity vector of this SoundObject
	 */
	Vector3 Sound3D::Velocity() const
	{
		float xyz[3];
		return Vector3(xyz[0], xyz[1], xyz[2]);
	}

	/**
	 * Sets the position of this SoundObject as relative to the global listener
	 * @param isrelative TRUE if the position of this SoundObject is relative
	 */
	void Sound3D::Relative(bool isrelative)
	{
	}

	/**
	 * @return TRUE if the position of this SoundObject is relative
	 */
	bool Sound3D::IsRelative() const
	{
		return false;
	}


	void Sound3D::MaxDistance(float maxdist)
	{
	}
	float Sound3D::MaxDistance() const
	{
		return 0.0f;
	}

	void Sound3D::RolloffFactor(float rolloff)
	{
	}
	float Sound3D::RolloffFactor() const
	{
		return 0.0f;
	}

	void Sound3D::ReferenceDistance(float refdist)
	{
	}
	float Sound3D::ReferenceDistance() const
	{
		return 0.0f;
	}

	void Sound3D::ConeOuterGain(float value)
	{
	}
	float Sound3D::ConeOuterGain() const
	{
		return 0.0f;
	}

	void Sound3D::ConeInnerAngle(float angle)
	{
	}
	float Sound3D::ConeInnerAngle() const
	{
		return 0.0f;
	}

	/**
	 * Sets the outer sound cone angle in degrees. Default is 360.
	 * Outer cone angle cannot be smaller than inner cone angle
	 * @param angle Angle 
	 */
	void Sound3D::ConeOuterAngle(float angle)
	{
		float innerAngle = ConeInnerAngle();
		if (angle < innerAngle)
			angle = innerAngle;
	}
	float Sound3D::ConeOuterAngle() const
	{
		return 0.0f;
	}












	/**
	 * Sets the master gain value (Volume) of the listener object for Audio3D.
	 * Valid range is [0.0 - Any], meaning the global volume can be increased
	 * until the sound starts distorting. WARNING! This does not change system volume!
	 * @param gain Gain value to set for the listener. Range[0.0 - Any].
	 */
	void Listener::Volume(float gain)
	{
		if (gain < 0.0f) gain = 0.0f;
		xMaster->SetVolume(gain);
	}

	/**
	 * Gets the master gain value (Volume) of the listener object for Audio3D.
	 * Valid range is [0.0 - Any], meaning the global volume can be increased
	 * until the sound starts distorting. WARNING! This does not change system volume!
	 * @return Master gain value (Volume). Range[0.0 - Any].
	 */
	float Listener::Volume()
	{
		float value;
		xMaster->GetVolume(&value);
		return value;
	}

	/**
	 * Sets the position of the listener object for Audio3D
	 * @param pos Vector containing x y z components of the position
	 */
	void Listener::Position(const Vector3& pos)
	{

	}

	/**
	 * Sets the position of the listener object for Audio3D
	 * @param x X component of the new position
	 * @param y Y component of the new position
	 * @param z Z component of the new position
	 */
	void Listener::Position(float x, float y, float z)
	{

	}

	/**
	 * Sets the position of the listener object for Audio3D
	 * @param xyz Float array containing x y z components of the position
	 */
	void Listener::Position(float* xyz)
	{

	}

	/**
	 * @return Position of the listener object for Audio3D
	 */
	Vector3 Listener::Position()
	{
		float xyz[3];
		return Vector3(xyz[0], xyz[1], xyz[2]);
	}

	/**
	 * Sets the velocity of the listener object for Audio3D
	 * @param pos Vector containing x y z components of the velocity
	 */
	void Listener::Velocity(const Vector3& vel)
	{

	}

	/**
	 * Sets the velocity of the listener object for Audio3D
	 * @param x X component of the new velocity
	 * @param y Y component of the new velocity
	 * @param z Z component of the new velocity
	 */
	void Listener::Velocity(float x, float y, float z)
	{

	}

	/**
	 * Sets the velocity of the listener object for Audio3D
	 * @param xyz Float array containing x y z components of the velocity
	 */
	void Listener::Velocity(float* xyz)
	{
	}

	/**
	 * @return Velocity of the listener object for Audio3D
	 */
	Vector3 Listener::Velocity()
	{
		float xyz[3];
		return Vector3(xyz[0], xyz[1], xyz[2]);
	}

	/**
	 * Sets the direction of the listener object for Audio3D
	 * @param target Vector containing x y z components of target where to 'look', thus changing the orientation.
	 * @param up Vector containing x y z components of the orientation 'up' vector
	 */
	void Listener::LookAt(const Vector3& target, const Vector3& up)
	{
		LookAt(target.x, target.y, target.z, up.x, up.y, up.z);
	}

	/**
	 * Sets the direction of the listener object for Audio3D
	 * @param x X component of the new direction
	 * @param y Y component of the new direction
	 * @param z Z component of the new direction
	 */
	void Listener::LookAt(float xAT, float yAT, float zAT, float xUP, float yUP, float zUP)
	{
		LookAt(Vector3(xAT, yAT, zAT), Vector3(xUP, yUP, zUP));
	}

	/**
	 * Sets the direction of the listener object for Audio3D
	 * @param xyzATxyzUP Float array containing x y z components of the direction
	 */
	void Listener::LookAt(float* xyzATxyzUP)
	{
	}

	/**
	 * @return Target position of the listener object's orientation
	 */
	Vector3 Listener::Target()
	{
		float xyzATxyzUP[6];
		return Vector3(xyzATxyzUP[0], xyzATxyzUP[1], xyzATxyzUP[2]);
	}

	/**
	 * @return Up vector of the listener object's orientation
	 */
	Vector3 Listener::Up()
	{
		float xyzATxyzUP[6];
		return Vector3(xyzATxyzUP[3], xyzATxyzUP[4], xyzATxyzUP[5]);
	}


} // namespace S3D
