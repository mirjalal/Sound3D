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

namespace S3D {

/**
 * Basic AudioStreamer class for streaming audio data.
 * Data is decoded and presented in simple wave PCM format.
 * 
 * The base implementation is actually the WAV streamer - other implementations
 * extend the virtual methods.
 */
class AudioStreamer
{
protected:
	int* FileHandle;				// internally interpreted file handle
	int StreamSize;					// size of the audiostream in PCM bytes, not File bytes
	int StreamPos;					// current stream position in PCM bytes
	unsigned short SampleRate;		// frequency (or rate) of the sound data, usually 20500 or 41000 (20.5kHz / 41kHz)
	unsigned char NumChannels;		// number of channels in a sample block, usually 1 or 2 (Mono / Stereo)
	unsigned char SampleBlockSize;	// size (in bytes) of a sample, usually 1 to 4 bytes (Mono8:1 / Stereo8:2 / Mono16:2 / Stereo16:4)

public:
	/**
	 * Creates a new uninitialized AudioStreamer.
	 * You should call OpenStream(file) to initialize the stream.
	 */
	AudioStreamer();

	/**
	 * Creates and Opens a new AudioStreamer.
	 * @param file Full path to the audiofile to stream
	 */
	AudioStreamer(const char* file);

	/**
	 * Destroys the AudioStream and frees all held resources
	 */
	virtual ~AudioStreamer();




	/**
	 * Opens a new stream for reading.
	 * @param file Audio file to open
	 * @return TRUE if stream is successfully opened and initialized. FALSE if the stream open failed or its already open.
	 */
	virtual bool OpenStream(const char* file);
	
	/**
	 * Closes the stream and releases all resources held.
	 */
	virtual void CloseStream();

	/**
	 * Reads some Audio data from the underlying stream.
	 * Audio data is decoded into PCM format, suitable for OpenAL.
	 * @param dstBuffer Destination buffer that receives the data
	 * @param dstSize Number of bytes to read. 64KB is good for streaming (gives ~1.5s of playback sound).
	 * @return Number of bytes read. 0 if stream is uninitialized or end of stream reached.
	 */
	virtual int ReadSome(void* dstBuffer, int dstSize);

	/**
	 * Seeks to the appropriate byte position in the stream.
	 * This value is between: [0...StreamSize]
	 * @param streampos Position in the stream to seek to in BYTES
	 * @return The actual position where seeked, or 0 if out of bounds (this also means the stream was reset to 0).
	 */
	virtual unsigned int Seek(unsigned int streampos);

	/**
	 * @return TRUE if the Stream has been opened. FALSE if it remains unopened.
	 */
	inline bool IsOpen() const { return FileHandle ? true : false; }

	/**
	 * Resets the stream position to the beginning.
	 */
	inline void ResetStream() { Seek(0); } 

	/**
	 * @return Size of the stream in PCM bytes, not File bytes
	 */
	inline int Size() const { return StreamSize; }

	/**
	 * @return Position of the stream in PCM bytes
	 */
	inline int Position() const { return StreamPos; }

	/** 
	 * @return TRUE if End Of Stream was reached
	 */
	inline bool IsEOS() const { return StreamPos == StreamSize; }

	/**
	 * @return Playback frequency specified by the streamer. Usually 20500 or 41000 (20.5kHz / 41kHz)
	 */
	inline int Frequency() const { return int(SampleRate); }

	/**
	 * @return Number of channels in this AudioStream. Usually 1 or 2 (Mono / Stereo)
	 */
	inline int Channels() const { return int(NumChannels); }

	/**
	 * @return Size (in bytes) of a single sample. Usually 1 to 4 bytes (Mono8:1 / Stereo8:2 / Mono16:2 / Stereo16:4)
	 */
	inline int SampleSize() const { return int(SampleBlockSize); }
};




/**
 * AudioStream for streaming file in WAV format.
 * The stream is decoded into PCM format.
 */
class WAVStreamer : public AudioStreamer
{
public:
	/** 
	 * Creates a new unitialized WAV AudioStreamer.
	 * You should call OpenStream(file) to initialize the stream. 
	 */
	inline WAVStreamer() : AudioStreamer() {}

	/**
	 * Creates and Initializes a new WAV AudioStreamer.
	 */
	inline WAVStreamer(const char* file) : AudioStreamer(file) {}
};




/**
 * AudioStream for streaming file in WAV format.
 * The stream is decoded into PCM format.
 */
class MP3Streamer : public AudioStreamer
{
public:
	/** 
	 * Creates a new unitialized MP3 AudioStreamer.
	 * You should call OpenStream(file) to initialize the stream. 
	 */
	MP3Streamer();

	/**
	 * Creates and Initializes a new MP3 AudioStreamer.
	 */
	MP3Streamer(const char* file);

	/**
	 * Destroys the MP3 AudioStream and frees all held resources
	 */
	virtual ~MP3Streamer();



	/**
	 * Opens a new stream for reading.
	 * @param file Audio file to open
	 * @return TRUE if stream is successfully opened and initialized. FALSE if the stream open failed or its already open.
	 */
	virtual bool OpenStream(const char* file);
	
	/**
	 * Closes the stream and releases all resources held.
	 */
	virtual void CloseStream();

	/**
	 * Reads some Audio data from the underlying stream.
	 * Audio data is decoded into PCM format, suitable for OpenAL.
	 * @param dstBuffer Destination buffer that receives the data
	 * @param dstSize Number of bytes to read. 64KB is good for streaming (gives ~1.5s of playback sound).
	 * @return Number of bytes read. 0 if stream is uninitialized or end of stream reached.
	 */
	virtual int ReadSome(void* dstBuffer, int dstSize);

	/**
	 * Seeks to the appropriate byte position in the stream.
	 * This value is between: [0...StreamSize]
	 * @param streampos Position in the stream to seek to in BYTES
	 * @return The actual position where seeked, or 0 if out of bounds (this also means the stream was reset to 0).
	 */
	virtual unsigned int Seek(unsigned int streampos);
};




/**
 * AudioStream for streaming file in WAV format.
 * The stream is decoded into PCM format.
 */
class OGGStreamer : public AudioStreamer
{
public:
	/** 
	 * Creates a new unitialized OGG AudioStreamer.
	 * You should call OpenStream(file) to initialize the stream. 
	 */
	OGGStreamer();

	/**
	 * Creates and Initializes a new OGG AudioStreamer.
	 */
	OGGStreamer(const char* file);

	/**
	 * Destroys the AudioStream and frees all held resources
	 */
	virtual ~OGGStreamer();


	/**
	 * Opens a new stream for reading.
	 * @param file Audio file to open
	 * @return TRUE if stream is successfully opened and initialized. FALSE if the stream open failed or its already open.
	 */
	virtual bool OpenStream(const char* file);
	
	/**
	 * Closes the stream and releases all resources held.
	 */
	virtual void CloseStream();

	/**
	 * Reads some Audio data from the underlying stream.
	 * Audio data is decoded into PCM format, suitable for OpenAL.
	 * @param dstBuffer Destination buffer that receives the data
	 * @param dstSize Number of bytes to read. 64KB is good for streaming (gives ~1.5s of playback sound).
	 * @return Number of bytes read. 0 if stream is uninitialized or end of stream reached.
	 */
	virtual int ReadSome(void* dstBuffer, int dstSize);

	/**
	 * Seeks to the appropriate byte position in the stream.
	 * This value is between: [0...StreamSize]
	 * @param streampos Position in the stream to seek to in BYTES
	 * @return The actual position where seeked, or 0 if out of bounds (this also means the stream was reset to 0).
	 */
	virtual unsigned int Seek(unsigned int streampos);
};














/**
 * Automatically creates a specific AudioStreamer
 * instance depending on the specified file extension
 * or by the file header format if extension not specified.
 * @note The Stream is not Opened! You must do it manually.
 * @param file Audio file string
 * @return New dynamic instance of a specific AudioStreamer. Or NULL if the file format cannot be detected.
 */
AudioStreamer* CreateAudioStreamer(const char* file);

/**
 * Automatically creates a specific AudioStreamer
 * instance depending on the specified file extension
 * or by the file header format if extension not specified.
 *
 * This initializes an already existing AudioStream instance.
 *
 * @note The Stream is not Opened! You must do it manually.
 * @param as AudioStream instance to create the streamer into. Can be any other AudioStream instance.
 * @param file Audio file string
 * @return TRUE if the instance was created.
 */
bool CreateAudioStreamer(AudioStreamer* as, const char* file);

}