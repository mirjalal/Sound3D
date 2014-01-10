#pragma once
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
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "AudioStreamer.h"
#include <vector>
#include "XAudio2_7\XAudio2.h"	// from DirectX SDK 2010
#include "XAudio2_7\X3DAudio.h" // from DirectX SDK 2010


namespace S3D
{

#if !VECTOR3_DEFINED
struct Vector3
{
	float x, y, z;
	inline Vector3() : x(0), y(0), z(0) {}
	inline Vector3(float x, float y, float z) : x(x), y(y), z(z) {}
};
#endif



/**
 * A Sound Object contains data on Sound playing state, volume, 3D position, etc.
 */
class SoundObject;



/** 
 * Our XAudio buffer object
 */
struct XABuffer : XAUDIO2_BUFFER
{
	WAVEFORMATEX wf;		// wave format descriptor
	int nBytesPerSample;	// number of bytes per single sample
	int nPCMSamples;		// number of PCM samples in the entire buffer
	unsigned wfHash;		// waveformat pseudo-hash
};



/**
 * A simple SoundBuffer designed for loading small sound files into a static buffer.
 * Should be used for sound files smaller than 64KB (1.5s @ 41kHz).
 */
class SoundBuffer
{
	friend class SoundObject;

protected:

	// number of references of this buffer held by SoundObjects; 
	// NOTE: SoundBuffer can't be Unloaded until refCount == 0.
	int refCount;				
	XABuffer* xaBuffer;			// sound buffer object
	
public:

	/**
	 * Creates a new SoundBuffer object
	 */
	SoundBuffer();

	/**
	 * Creates a new SoundBuffer and loads the specified sound file
	 * @param file Path to sound file to load
	 */
	SoundBuffer(const char* file);

	/**
	 * Destroyes and unloads this buffer
	 */
	virtual ~SoundBuffer();

	/**
	 * @return Frequency in Hz of this SoundBuffer data
	 */
	int Frequency() const;

	/**
	 * @return Number of bits in a sample of this SoundBuffer data (8 or 16)
	 */
	int SampleBits() const;	

	/**
	 * @return Number of bytes in a sample of this SoundBuffer data (1 or 2)
	 */
	int SampleBytes() const;

	/**
	 * @return Number of sound channels in the SoundBuffer data (1 or 2)
	 */
	int Channels() const;	

	/** 
	 * @return Size of a sample block in bytes [LL][RR] (1 to 4)
	 */
	int SampleSize() const;

	/**
	 * @return Size of this SoundBuffer or SoundStream in BYTES
	 */
	int SizeBytes() const;

	/**
	 * @return Size of this SoundBuffer in PCM SAMPLES
	 */
	virtual int Size() const;		

	/**
	 * @return Number of SoundObjects that still reference this SoundBuffer.
	 */
	int RefCount() const;

	/**
	 * @return TRUE if this object is a Sound stream
	 */
	virtual bool IsStream() const;

	/**
	 * @return Wave format descriptor for this buffer
	 */
	const WAVEFORMATEX* WaveFormat() const;

	/**
	 * @return Unique hash to distinguish between different Wave formats
	 */
	unsigned WaveFormatHash() const;	

	/**
	 * Loads this SoundBuffer with data found in the specified file.
	 * Supported formats: .wav .mp3
	 * @param file Sound file to load
	 * @return TRUE if loading succeeded and a valid buffer was created.
	 */
	virtual bool Load(const char* file);

	/**
	 * Tries to release the underlying sound buffer and free the memory.
	 * @note This function will fail if refCount > 0. This means there are SoundObjects still using this SoundBuffer
	 * @return TRUE if SoundBuffer data was freed, FALSE if SoundBuffer is still used by a SoundObject.
	 */
	virtual bool Unload();

	/**
	 * Binds a specific source to this SoundBuffer and increases the refCount.
	 * @param so SoundObject to bind to this SoundBuffer.
	 * @return FALSE if the binding failed. TODO: POSSIBLE REASONS?
	 */
	virtual bool BindSource(SoundObject* so);

	/**
	 * Unbinds a specific source from this SoundBuffer and decreases the refCount.
	 * @param so SoundObject to unbind from this SoundBuffer.
	 * @return FALSE if the unbinding failed. TODO: POSSIBLE REASONS?
	 */
	virtual bool UnbindSource(SoundObject* so);

	/**
	 * Resets the buffer in the context of the specified SoundObject
	 * @note Calls ResetStream on AudioStreams.
	 * @param so SoundObject to reset this buffer for
	 * @return TRUE if reset was successful
	 */
	virtual bool ResetBuffer(SoundObject* so);

};








/**
 * SoundStream stream audio data from a file source.
 * Extremely useful for large file playback. Even a 4m long mp3 can take over 40mb of ram.
 * Multiple sources can be bound to this stream.
 */
class SoundStream : public SoundBuffer
{
protected:
	struct SO_ENTRY 
	{ 
		SoundObject* obj; 
		int base; // current PCM block offset
		int next; // the next PCM block offset to load

		XABuffer* front;	// currently playing buffer - frontbuffer
		XABuffer* back;		// enqueued backbuffer

		inline SO_ENTRY(SoundObject* obj, int base, int next) 
			: obj(obj), base(base), next(next), front(0), back(0)
		{
		} 
	};
	static const int STREAM_BUFFERSIZE = 65536;	// 64KB
	
	std::vector<SO_ENTRY> alSources;	// bound sources
	AudioStreamer* alStream;			// streamer object

public:

	/**
	 * Creates a new SoundsStream object
	 */
	SoundStream();

	/**
	 * Creates a new SoundStream object and loads the specified sound file
	 * @param file Path to the sound file to load
	 */
	SoundStream(const char* file);

	/**
	 * Destroys and unloads any resources held
	 */
	virtual ~SoundStream();

	/**
	 * @return Size of this SoundStream in PCM SAMPLES
	 */
	virtual int Size() const override;

	/**
	 * @return TRUE if this object is a Sound stream
	 */
	virtual bool IsStream() const override;

	/**
	 * Initializes this SoundStream with data found in the specified file.
	 * Supported formats: .wav .mp3
	 * @param file Sound file to load
	 * @return TRUE if loading succeeded and a stream was initialized.
	 */
	virtual bool Load(const char* file) override;

	/**
	 * Tries to release the underlying sound buffers and free the memory.
	 * @note This function will fail if refCount > 0. This means there are SoundObjects still using this SoundStream
	 * @return TRUE if SoundStream data was freed, FALSE if SoundStream is still used by a SoundObject.
	 */
	virtual bool Unload() override;

	/**
	 * Binds a specific source to this SoundStream and increases the refCount.
	 * @param so SoundObject to bind to this SoundStream.
	 * @return FALSE if the binding failed. TODO: POSSIBLE REASONS?
	 */
	virtual bool BindSource(SoundObject* so) override;

	/**
	 * Unbinds a specific source from this SoundStream and decreases the refCount.
	 * @param so SoundObject to unbind from this SoundStream.
	 * @return FALSE if the unbinding failed. TODO: POSSIBLE REASONS?
	 */
	virtual bool UnbindSource(SoundObject* so) override;

	/**
	 * Resets the buffer in the context of the specified SoundObject
	 * @note Calls ResetStream on AudioStreams.
	 * @param so SoundObject to reset this buffer for
	 * @return TRUE if reset was successful
	 */
	virtual bool ResetBuffer(SoundObject* so) override;

	/**
	 * Resets the stream by unloading previous buffers and requeuing the first two buffers.
	 * @param so SoundObject to reset the stream for
	 * @return TRUE if stream was successfully reloaded
	 */
	bool ResetStream(SoundObject* so);

	/**
	 * Streams the next Buffer block from the stream.
	 * @param so Specific SoundObject to stream with.
	 * @return TRUE if a buffer was streamed. FALSE if EOS()
	 */
	bool StreamNext(SoundObject* so);

	/**
	 * @param so Specific SoundObject to check for end of stream
	 * @return TRUE if End of Stream was reached or if there is no stream loaded
	 */
	bool IsEOS(const SoundObject* so) const;

	/**
	 * @param so SoundObject to query index of
	 * @return A valid pointer if this SoundObject is bound. Otherwise NULL.
	 */
	SO_ENTRY* GetSOEntry(const SoundObject* so) const;

	/**
	 * Seeks to the specified sample position in the stream
	 * @param so SoundObject to perform seek on
	 * @param samplepos Position in the stream in samples [0..SoundStream::Size()]
	 */
	void Seek(SoundObject* so, int samplepos);

protected:

	/**
	 * Internal stream function.
	 * @param soe SoundObject Entry to stream
	 * @return TRUE if a buffer was streamed
	 */
	bool StreamNext(SO_ENTRY& soe);

	/**
	 * [internal] Load streaming data into the specified SoundObject
	 * at the optionally specified streamposition.
	 * @param so SoundObject to queue with stream data
	 * @param streampos [optional] PCM byte position in stream where to seek data from. 
	 *                  If unspecified (default -1), stream will use the current streampos
	 */
	bool LoadStreamData(SoundObject* so, int streampos = -1);

	/**
	 * [internal] Unloads all queued data for the specified SoundObject
	 * @param so SoundObject to unqueue and unload data for
	 */
	void ClearStreamData(SoundObject* so);
};












/**
 * Holds and manages the current state of a SoundObject
 */
struct SoundObjectState; 



/**
 * Base for all 3D and Ambient sound objects
 */
class SoundObject
{
protected:
	friend class SoundBuffer;			// give soundbuffer access to the internals of this object
	friend class SoundStream;			// give soundstream access to the internals of this object
	SoundBuffer* Sound;					// soundbuffer or stream to use
	IXAudio2SourceVoice* Source;		// the sound source generator (interfaces XAudio2 to generate waveforms)
	SoundObjectState* State;			// Holds and manages the current state of a SoundObject
	X3DAUDIO_EMITTER Emitter;			// 3D sound emitter data (this object)

	/**
	 * Creates an uninitialzed empty SoundObject
	 */
	SoundObject();
	/**
	 * Creates a soundobject with an attached buffer
	 * @param sound SoundBuffer this object uses for playing sounds
	 * @param loop True if sound looping is wished
	 * @param play True if sound should start playing immediatelly
	 */
	SoundObject(SoundBuffer* sound, bool loop = false, bool play = false);
	~SoundObject(); // unhooks any sounds and frees resources

public:
	/**
	 * Sets the SoundBuffer or SoundStream for this SoundObject. Set NULL to remove and unbind the SoundBuffer.
	 * @param sound Sound to bind to this object. Can be NULL to unbind sounds from this object.
	 * @param loop [optional] Sets the sound looping or non-looping. Streams cannot be looped.
	 */
	void SetSound(SoundBuffer* sound, bool loop = false);

	/**
	 * @return Current soundbuffer set to this soundobject
	 */
	inline SoundBuffer* GetSound() const { return Sound; }

	/**
	 * @return TRUE if this SoundObject has an attached SoundStream that can be streamed.
	 */
	bool IsStreamable() const;

	/**
	 * @return TRUE if this SoundObject is streamable and the End Of Stream was reached.
	 */
	bool IsEOS() const;

	/**
	 * Starts playing the sound. If the sound is already playing, it is rewinded and played again from the start.
	 */
	void Play();

	/**
	 * Starts playing a new sound. Any older playing sounds will be stopped and replaced with this sound.
	 * @param sound SoundBuffer or SoundStream to start playing
	 * @param loop [false] Sets if the sound is looping or not. Streams are never loopable.
	 */
	void Play(SoundBuffer* sound, bool loop = false);

	/**
	 * Stops playing the sound and unloads streaming buffers.
	 */
	void Stop();

	/**
	 * Temporarily pauses sound playback and doesn't unload any streaming buffers.
	 */
	void Pause();

	/**
	 * If current status is Playing, then this rewinds to start of the soundbuffer/stream and starts playing again.
	 * If current status is NOT playing, then any stream resources are freed and the object is reset.
	 */
	void Rewind();

	/**
	 * @return TRUE if the sound source is PLAYING.
	 */
	bool IsPlaying() const;

	/**
	 * @return TRUE if the sound source is STOPPED.
	 */
	bool IsStopped() const;

	/**
	 * @return TRUE if the sound source is PAUSED.
	 */
	bool IsPaused() const;

	/**
	 * @return TRUE if the sound source is at the beginning of the sound buffer.
	 */
	bool IsInitial() const;

	/**
	 * @return TRUE if the sound source is in LOOPING mode.
	 */
	bool IsLooping() const;

	/**
	 * Sets the looping mode of the sound source.
	 */
	void Looping(bool looping);

	/**
	 * Indicates the gain (volume amplification) applied. Range [0.0f .. 1.0f]
	 * Each division by 2 equals an attenuation of -6dB. Each multiplicaton with 2 equals an amplification of +6dB.
	 * A value of 0.0 is meaningless with respect to a logarithmic scale; it is interpreted as zero volume - the channel is effectively disabled.
	 * @param gain Gain value between 0.0..1.0
	 */
	void Volume(float gain);

	/**
	 * @return Current gain value of this source
	 */
	float Volume() const;

	/**
	 * @return Gets the current playback position in the SoundBuffer or SoundStream in SAMPLES
	 */
	int PlaybackPos() const;

	/**
	 * Sets the playback position of the underlying SoundBuffer or SoundStream in SAMPLES
	 * @param seekpos Position in SAMPLES where to seek in the SoundBuffer or SoundStream [0..PlaybackSize]
	 */
	void PlaybackPos(int seekpos);

	/**
	 * @return Playback size of the underlying SoundBuffer or SoundStream in SAMPLES
	 */
	int PlaybackSize() const;

	/**
	 * @return Number of samples processed every second (aka SampleRate or Frequency)
	 */
	int SamplesPerSecond() const;
};




/**
 * A positionless sound object used for playing background music or sounds.
 */
class Sound : public SoundObject
{
public:

	/**
	 * Creates an uninitialzed Sound2D, but also
	 * setups the sound source as an environment/bakcground sound.
	 */
	Sound();

	/**
	 * Creates a Sound2D with an attached buffer
	 * @param sound SoundBuffer/SoundStream this object can use for playing sounds
	 * @param loop True if sound looping is wished
	 * @param play True if sound should start playing immediatelly
	 */
	Sound(SoundBuffer* sound, bool loop = false, bool play = false);

	/**
	 * Resets all background audio parameters to their defaults
	 */
	void Reset();
};




/**
 * A 3D positional sound object used for playing environment-aware music or sounds.
 */
class Sound3D : public SoundObject
{
public:

	/**
	 * Creates an uninitialzed Sound3D, but also
	 * setups the sound source as a 3D positional sound.
	 */
	Sound3D();

	/**
	 * Creates a Sound3D with an attached buffer
	 * @param sound SoundBuffer/SoundStream this object can use for playing sounds
	 * @param loop True if sound looping is wished
	 * @param play True if sound should start playing immediatelly
	 */
	Sound3D(SoundBuffer* sound, bool loop = false, bool play = false);

	/**
	 * Resets all 3D positional audio parameters to their defaults
	 */
	void Reset();

	/**
	 * Sets the 3D position of this Sound3D object
	 * @param x Position x component
	 * @param y Position Y component
	 * @param z Position z component
	 */
	void Position(float x, float y, float z);

	/**
	 * Sets the 3D position of this Sound3D object
	 * @param xyz Float vector containing 3D position X Y Z components
	 */
	void Position(float* xyz);

	/**
	 * @return 3D position vector of this SoundObject
	 */
	Vector3 Position() const;

	/**
	 * Sets the 3D direction of this Sound3D object
	 * @param x Direction x component
	 * @param y Direction Y component
	 * @param z Direction z component
	 */
	void Direction(float x, float y, float z);
	
	/**
	 * Sets the 3D direction of this Sound3D object
	 * @param xyz Float vector containing 3D direction X Y Z components
	 */
	void Direction(float* xyz);

	/**
	 * @return 3D direction vector of this SoundObject
	 */
	Vector3 Direction() const;

	/**
	 * Sets the 3D velocity of this Sound3D object
	 * @param x Velocity x component
	 * @param y Velocity Y component
	 * @param z Velocity z component
	 */
	void Velocity(float x, float y, float z);

	/**
	 * Sets the 3D velocity of this Sound3D object
	 * @param xyz Float vector containing 3D velocity X Y Z components
	 */
	void Velocity(float* xyz);

	/**
	 * @return 3D velocity vector of this SoundObject
	 */
	Vector3 Velocity() const;

	/**
	 * Sets the position of this SoundObject as relative to the global listener
	 * @param isrelative TRUE if the position of this SoundObject is relative
	 */
	void Relative(bool isrelative);

	/**
	 * @return TRUE if the position of this SoundObject is relative
	 */
	bool IsRelative() const;


	void MaxDistance(float maxdist);
	float MaxDistance() const;

	void RolloffFactor(float rolloff);
	float RolloffFactor() const;

	void ReferenceDistance(float refdist);
	float ReferenceDistance() const;

	/**
	 * The inner cone angle is where the listener hears no extra attenuation (ie, you 
		hear the regular distance-based attenuation), while the outer cone is where 
		the AL_CONE_OUTER_GAIN fully applied (ie, you hear the distance-based 
		attenuation as well as AL_CONE_OUTER_GAIN both applied).

		> 2. if outer cone angle is 360 deg, and inner cone angle is less than that,
		> can the source still be heard from listener at angle > inner angle

		Yes, with varying degrees of attenuation (the closer to the outer cone angle 
		you are, the more AL_CONE_OUTER_GAIN is applied).

		> 3. does it still valid if outer cone angle is larger than inner cone angle ?

		Yes. In fact, if the outer cone angle is smaller than the inner cone angle, 
		then the cone attenuation won't work properly.

		> 4. how does the outer cone gain (angle-dependent attenuation) will be
		> applied to source gain (in equation) ?

		When the listener is at (or beyond) the outer cone angle, the source gain and 
		outer cone gain are multiplied together. When the listener is at (or within) 
		the inner cone angle, the outer cone gain isn't applied at all.

		If the listener is between the inner cone angle and outer cone angle, then 
		OpenAL will only partially apply the outer cone gain. The exact method is 
		implementation dependent, but it is a smooth curve from no extra attenuation 
		to full attenuation (a linear slope is not uncommon, but it can be something 
		else).

		> 5. what's the default value for inner, outer cone angle, and outer cone
		> gain ?

		The inner cone angle and outer cone angle default to 360 degrees. The outer 
		cone gain defaults to 1.0.

		Note that the angle between the source and listener only gets up to 180 
		degrees (when the listener is directly behind the source), so the actual total 
		area covered by the cones is double the value you specify.. eg, an inner angle 
		of 90 with the listener directly to the left or right of the source will not 
		be attenuated by the cones; 180 degree coverage.
	 */
	void ConeOuterGain(float value);
	float ConeOuterGain() const;

	void ConeInnerAngle(float angle);
	float ConeInnerAngle() const;

	/**
	 * Sets the outer sound cone angle in degrees. Default is 360.
	 * Outer cone angle cannot be smaller than inner cone angle
	 * @param angle Angle 
	 */
	void ConeOuterAngle(float angle);
	float ConeOuterAngle() const;
};


/**
 * Sound3D sound listener
 */
class Listener
{
public:

	/**
	 * Sets the master gain value (Volume) of the listener object for Audio3D.
	 * Valid range is [0.0 - Any], meaning the global volume can be increased
	 * until the sound starts distorting. WARNING! This does not change system volume!
	 * @param gain Gain value to set for the listener. Range[0.0 - Any].
	 */
	static void Volume(float gain);

	/**
	 * Gets the master gain value (Volume) of the listener object for Audio3D.
	 * Valid range is [0.0 - Any], meaning the global volume can be increased
	 * until the sound starts distorting. WARNING! This does not change system volume!
	 * @return Master gain value (Volume). Range[0.0 - Any].
	 */
	static float Volume();

	/**
	 * Sets the position of the listener object for Audio3D
	 * @param pos Vector containing x y z components of the position
	 */
	static void Position(const Vector3& pos);

	/**
	 * Sets the position of the listener object for Audio3D
	 * @param x X component of the new position
	 * @param y Y component of the new position
	 * @param z Z component of the new position
	 */
	static void Position(float x, float y, float z);

	/**
	 * Sets the position of the listener object for Audio3D
	 * @param xyz Float array containing x y z components of the position
	 */
	static void Position(float* xyz);

	/**
	 * @return Position of the listener object for Audio3D
	 */
	static Vector3 Position();

	/**
	 * Sets the velocity of the listener object for Audio3D
	 * @param pos Vector containing x y z components of the velocity
	 */
	static void Velocity(const Vector3& vel);

	/**
	 * Sets the velocity of the listener object for Audio3D
	 * @param x X component of the new velocity
	 * @param y Y component of the new velocity
	 * @param z Z component of the new velocity
	 */
	static void Velocity(float x, float y, float z);

	/**
	 * Sets the velocity of the listener object for Audio3D
	 * @param xyz Float array containing x y z components of the velocity
	 */
	static void Velocity(float* xyz);

	/**
	 * @return Velocity of the listener object for Audio3D
	 */
	static Vector3 Velocity();

	/**
	 * Sets the direction of the listener object for Audio3D
	 * @param target Vector containing x y z components of target where to 'look', thus changing the orientation.
	 * @param up Vector containing x y z components of the orientation 'up' vector
	 */
	static void LookAt(const Vector3& target, const Vector3& up);

	/**
	 * Sets the direction of the listener object for Audio3D
	 * @param x X component of the new direction
	 * @param y Y component of the new direction
	 * @param z Z component of the new direction
	 */
	static void LookAt(float xAT, float yAT, float zAT, float xUP, float yUP, float zUP);

	/**
	 * Sets the direction of the listener object for Audio3D
	 * @param xyzATxyzUP Float array containing x y z components of the direction
	 */
	static void LookAt(float* xyzATxyzUP);

	/**
	 * @return Target position of the listener object's orientation
	 */
	static Vector3 Target();

	/**
	 * @return Up vector of the listener object's orientation
	 */
	static Vector3 Up();

};

} // namespace S3D