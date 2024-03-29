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
#include "AudioStreamer.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>	// LoadLibrary, FreeLibrary
#include <stdio.h>		// fopen
#include <stdlib.h>		// printf
#include <sys/types.h>	// off_t

#ifdef _DEBUG
	#define indebug(x) x
#else
	#define indebug(x) // do nothing in release
#endif

// declare the SHARED segment
#pragma comment(linker, "/section:SHARED,RWS")

// declare placement new and matching delete
static inline void* operator new(size_t sz, void* dst) { return dst; }
static inline void operator delete(void* mem, void* dst) { ; }

namespace S3D 
{

	//// Low Latency File IO straight to Windows API, with no beating around the bush

	// opens a file with read-only rights
	inline void* file_open_ro(const char* filename)
	{
		void* fh = CreateFileA(filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (fh == INVALID_HANDLE_VALUE) return NULL;
		return fh;
	}
	// close the file
	inline int file_close(void* handle)
	{
		CloseHandle(handle);
		return 0;
	}
	// reads bytes from an opened file
	inline int file_read(void* handle, void* dst, size_t size)
	{
		DWORD bytesRead;
		ReadFile(handle, dst, size, &bytesRead, NULL);
		return (int)bytesRead;
	}
	// seeks the file pointer
	inline off_t file_seek(void* handle, off_t offset, int whence)
	{
		return SetFilePointer(handle, offset, NULL, whence);
	}
	// tells the current file position
	inline off_t file_tell(void* handle)
	{
		return SetFilePointer(handle, 0, NULL, FILE_CURRENT);
	}




	////
	// Getting Audio file format impl.
	////////

#pragma region GetAudioFileFormat

	enum AudioFileFormat { INVALID, WAV, MP3, OGG, };
	
	static int GetExtension(const char* file) // returns the extension in the string as an integer
	{
		const char* ext = strrchr(file, '.');
		if (!ext || ext == file)
			return 0; // no extension || start of filename was '.'
		return *(int*)ext; // force the extension bytes into an int
	}
	

	static AudioFileFormat GetAudioFileFormatByExtension(const char* file) // pretty cheap actually - we just check the file extension!
	{
		int ext = GetExtension(file);
		if (!ext) return AudioFileFormat::INVALID; // no extension or no file found.
		switch (ext) // interpret the extension as an int
		{ // abuse little endian byte order:
		case 'vaw.': return AudioFileFormat::WAV; // WAV file
		case '3pm.': return AudioFileFormat::MP3; // MP3 file
		case 'ggo.': return AudioFileFormat::OGG; // Ogg Vorbis file
		default:
			indebug(printf("Unsupported audio file extension: %s\n", file));
			indebug(printf("Supported formats: wav mp3 ogg\n"));
			return AudioFileFormat::INVALID; // error
		}
	}

	static bool checkMP3Tag(void* buffer)
	{
#pragma pack(push)
#pragma pack(1)
		struct MP3TAGV2 {
			char id3[3];
			unsigned char vermajor;
			unsigned char verminor;
			unsigned char flags;
			unsigned int size;
		};
#pragma pack(pop)
		MP3TAGV2 tag = *(MP3TAGV2*)buffer;
		if (tag.id3[0] == 'I' && tag.id3[1] == 'D' && tag.id3[2] == '3')
			return true;
		return false; // not an mp3 header
	}

	static AudioFileFormat GetAudioFileFormatByHeader(const char* file) // a bit heavier - we actually check the file header
	{
		FILE* f = fopen(file, "rb"); // open file 'read-binary'
		if (f == NULL)
		{
			indebug(printf("File not found: \"%s\"\n", file));
			return AudioFileFormat::INVALID; // file doesn't exist
		}
		// MP3 has a header tag, needs 10 bytes
		// WAV has a large header with byte fields [file + 0]='RIFF' and [file + 8]='WAVE', needs 12bytes
		// OGG has a 32-bit "capture pattern" sync field 'OggS', needs 4 bytes
		int buffer[3]; // WAV requires most, so 12 bytes
		fread(buffer, sizeof(buffer), 1, f);
		fclose(f); f = NULL;

		if (buffer[0] == 'FFIR' && buffer[2] == 'EVAW')
			return AudioFileFormat::WAV;
		else if (buffer[0] == 'SggO')
			return AudioFileFormat::OGG;
		else if (checkMP3Tag(buffer))
			return AudioFileFormat::MP3;
		return AudioFileFormat::INVALID;
	}

	AudioStreamer* CreateAudioStreamer(const char* file)
	{
		AudioFileFormat fmt = GetAudioFileFormatByExtension(file);
		if (!fmt) fmt = GetAudioFileFormatByHeader(file);
		if (!fmt) return nullptr; // unsupported format
		switch(fmt) {
			case AudioFileFormat::WAV: return new WAVStreamer();
			case AudioFileFormat::MP3: return new MP3Streamer();
			case AudioFileFormat::OGG: return new OGGStreamer();
		}
		return nullptr; // ok?... unsupported format
	}

	bool CreateAudioStreamer(AudioStreamer* as, const char* file)
	{
		if (!as) return false; // oh well...
		as->CloseStream(); // just in case...
		
		AudioFileFormat fmt = GetAudioFileFormatByExtension(file);
		if (!fmt) fmt = GetAudioFileFormatByHeader(file);
		if (!fmt) return false; // unsupported format
		switch (fmt) {
			case AudioFileFormat::WAV: new (as) WAVStreamer(); break;
			case AudioFileFormat::MP3: new (as) MP3Streamer(); break;
			case AudioFileFormat::OGG: new (as) OGGStreamer(); break;
			default: return false; // unsupported format
		}
		return true; // everything went ok
	}
#pragma endregion





	//////
	// AudioStreamer and WAVStreamer impl
	//

#pragma region WAVStreamer

struct RIFFCHUNK
{
	union {
		int ID;			// chunk ID
		char IDStr[4];	// chunk ID as string
	};
	int Size;			// chunk SIZE
};

struct WAVHEADER
{
	RIFFCHUNK Header;	// Contains the letters "RIFF" && (SizeOfFile - 8) in bytes
	union {
		int Format;				// Contains the letters "WAVE"
		char FormatAsStr[4];
	};
	// The FMT sub-chunk
	RIFFCHUNK Subchunk1;		// Contains the letters "fmt " && 16 bytes in size
	struct {
		short AudioFormat;		// Should be 1, otherwise this file is compressed!
		short NumChannels;		// Mono = 1, Stereo = 2
		int SampleRate;			// 8000, 22050, 44100, etc
		int ByteRate;			// == SampleRate * NumChannels * BitsPerSample/8
		short BlockAlign;		// == NumChannels * BitsPerSample/8
		short BitsPerSample;	// 8 bits == 8, 16 bits == 16, etc.
	};
	RIFFCHUNK NextChunk1;		// Contains the letters "info" or "data"
	int someData[4];
	RIFFCHUNK NextChunk2;		// maybe this one is data?

	RIFFCHUNK* getDataChunk()
	{
		if (NextChunk1.ID == (int)'atad') return &NextChunk1;
		if (NextChunk2.ID == (int)'atad') return &NextChunk2;
		return nullptr;
	}
};

	/**
	 * Creates a new uninitialized AudioStreamer.
	 * You should call OpenStream(file) to initialize the stream.
	 */
	AudioStreamer::AudioStreamer()
		: FileHandle(0), StreamSize(0), StreamPos(0), SampleRate(0), NumChannels(0), SampleSize(0), SampleBlockSize(0)
	{
	}

	/**
	 * Creates and Opens a new AudioStreamer.
	 * @param file Full path to the audiofile to stream
	 */
	AudioStreamer::AudioStreamer(const char* file)
		: FileHandle(0), StreamSize(0), StreamPos(0), SampleRate(0), NumChannels(0), SampleSize(0), SampleBlockSize(0)
	{
		OpenStream(file);
	}

	/**
	 * Destroys the AudioStream and frees all held resources
	 */
	AudioStreamer::~AudioStreamer()
	{
		CloseStream();
	}




	/**
	 * Opens a new stream for reading.
	 * @param file Audio file to open
	 * @return TRUE if stream is successfully opened and initialized. FALSE if the stream open failed or its already open.
	 */
	bool AudioStreamer::OpenStream(const char* file)
	{
		if (FileHandle) // dont allow reopen an existing stream
			return false;
		
		if (!(FileHandle = (int*)file_open_ro(file))) {
			indebug(printf("Failed to open file: \"%s\"\n", file));
			return false; // oh well;
		}

		WAVHEADER wav;
		if (file_read(FileHandle, &wav, sizeof(wav)) <= 0) {
			indebug(printf("Failed to load WAV header: \"%s\"\n", file));
			return false; // invalid file
		}
		if (wav.Header.ID != (int)'FFIR' || wav.Format != (int)'EVAW') { // != "RIFF" || != "WAVE"
			indebug(printf("Invalid WAV file header: %s\n", file));
			return false; // invalid wav header
		}

		RIFFCHUNK* dataChunk = wav.getDataChunk();
		if (!dataChunk) {
			indebug(printf("Failed to find WAV <data> chunk.\n"));
			return false; // invalid WAV file
		}
		// initialize essential variables
		StreamSize = dataChunk->Size;
		SampleRate = (unsigned int)wav.SampleRate;
		NumChannels = (unsigned char)wav.NumChannels;
		SampleSize = (unsigned char)(wav.BitsPerSample >> 3);	// BPS/8 => SampleSize
		SampleBlockSize = SampleSize * NumChannels;				// [LL][RR] (1 to 4 bytes)
		return true; // everything went ok
	}
	
	/**
	 * Closes the stream and releases all resources held.
	 */
	void AudioStreamer::CloseStream()
	{
		if (FileHandle)
		{
			file_close(FileHandle);
			FileHandle = 0;
			StreamSize = 0;
			StreamPos = 0;
			SampleRate = 0;
			NumChannels = 0;
			SampleSize = 0;
			SampleBlockSize = 0;
		}
	}

	/**
	 * Reads some Audio data from the underlying stream.
	 * Audio data is decoded into PCM format, suitable for OpenAL.
	 * @param dstBuffer Destination buffer that receives the data
	 * @param dstSize Number of bytes to read. 64KB is good for streaming (gives ~1.5s of playback sound).
	 * @return Number of bytes read. 0 if stream is uninitialized or end of stream reached.
	 */
	int AudioStreamer::ReadSome(void* dstBuffer, int dstSize)
	{
		if (!FileHandle)
			return 0; // nothing to do here
		int count = StreamSize - StreamPos; // calc available data from stream
		if (count == 0) // if stream available bytes 0?
			return 0; // EOS reached
		if (count > dstSize) // if stream has more data than buffer
			count = dstSize; // set bytes to read bigger
		count -= count % SampleBlockSize; // make sure count is aligned to blockSize

		if (file_read(FileHandle, dstBuffer, count) <= 0) {
			StreamPos = StreamSize; // set EOS
			return 0; // no bytes read
		}
		StreamPos += count;
		return count;
	}

	/**
	 * Seeks to the appropriate byte position in the stream.
	 * This value is between: [0...StreamSize]
	 * @param streampos Position in the stream to seek to in BYTES
	 * @return The actual position where seeked, or 0 if out of bounds (this also means the stream was reset to 0).
	 */
	unsigned int AudioStreamer::Seek(unsigned int streampos)
	{
		if (int(streampos) >= StreamSize)
			streampos = 0;
		streampos -= streampos % SampleBlockSize; // align to PCM blocksize
		int actual = streampos + sizeof(WAVHEADER); // skip the WAVHEADER
		file_seek(FileHandle, actual, SEEK_SET);
		StreamPos = streampos;
		return streampos;
	}

#pragma endregion





	//////
	// MP3Streamer impl
	//

#pragma region MP3Streamer

#pragma data_seg("SHARED")
static HMODULE mpgDll = NULL;
static void (*mpg_exit)();
static int (*mpg_init)();
static int* (*mpg_new)(const char* decoder, int* error);
static int (*mpg_close)(int* mh);
static void (*mpg_delete)(int* mh);
static int (*mpg_open_handle)(int* mh, void* iohandle);
static int (*mpg_getformat)(int* mh, long* rate, int* channels, int* encoding);
static size_t (*mpg_length)(int* mh);
static size_t (*mpg_outblock)(int* mh);
static int (*mpg_encsize)(int encoding);
static int (*mpg_read)(int* mh, unsigned char* outmemory, size_t outmemsize, size_t* done);
static const char* (*mpg_strerror)(int* mh);
static int (*mpg_errcode)(int* mh);
static const char** (*mpg_supported_decoders)();
static size_t (*mpg_seek)(int* mh, size_t sampleOffset, int whence);
static const char* (*mpg_current_decoder)(int* mh);

typedef int (*mpg_read_func)(void*, void*, size_t);
typedef off_t (*mpg_seek_func)(void*, off_t, int);
typedef int (*mpg_close_func)(void*);
static int(*mpg_replace_reader_handle)(int* mh, mpg_read_func, mpg_seek_func, mpg_close_func);
#pragma data_seg()

template<class Proc> static inline void LoadMpgProc(Proc& outProcVar, const char* procName)
{
	outProcVar = (Proc)GetProcAddress(mpgDll, procName);
}
static void _UninitMPG()
{
	mpg_exit();
	FreeLibrary(mpgDll);
	mpgDll = 0;
}
static void _InitMPG()
{
	static const char* mpglib = "libmpg123";
	if (!(mpgDll = LoadLibraryA(mpglib)))
	{
		printf("Failed to load DLL %s!\n", mpglib);
		return;
	}
	LoadMpgProc(mpg_exit, "mpg123_exit");
	LoadMpgProc(mpg_init, "mpg123_init");
	LoadMpgProc(mpg_new, "mpg123_new");
	LoadMpgProc(mpg_close, "mpg123_close");
	LoadMpgProc(mpg_delete, "mpg123_delete");
	LoadMpgProc(mpg_open_handle, "mpg123_open_handle");
	LoadMpgProc(mpg_getformat, "mpg123_getformat");
	LoadMpgProc(mpg_length, "mpg123_length");
	LoadMpgProc(mpg_outblock, "mpg123_outblock");
	LoadMpgProc(mpg_encsize, "mpg123_encsize");
	LoadMpgProc(mpg_read, "mpg123_read");
	LoadMpgProc(mpg_strerror, "mpg123_strerror");
	LoadMpgProc(mpg_errcode, "mpg123_errcode");
	LoadMpgProc(mpg_supported_decoders, "mpg123_supported_decoders");
	LoadMpgProc(mpg_seek, "mpg123_seek");
	LoadMpgProc(mpg_current_decoder, "mpg123_current_decoder");
	LoadMpgProc(mpg_replace_reader_handle, "mpg123_replace_reader_handle");
	mpg_init();
	atexit(_UninitMPG);
}





	/** 
	 * Creates a new unitialized MP3 AudioStreamer.
	 * You should call OpenStream(file) to initialize the stream. 
	 */
	MP3Streamer::MP3Streamer() : AudioStreamer()
	{
		if (!mpgDll) _InitMPG();
	}

	/**
	 * Creates and Initializes a new MP3 AudioStreamer.
	 */
	MP3Streamer::MP3Streamer(const char* file) : AudioStreamer()
	{
		if (!mpgDll) _InitMPG();
		OpenStream(file);
	}

	/**
	 * Destroys the AudioStream and frees all held resources
	 */
	MP3Streamer::~MP3Streamer()
	{
		CloseStream();
	}

	/**
	 * Opens a new stream for reading.
	 * @param file Audio file to open
	 * @return TRUE if stream is successfully opened and initialized. FALSE if the stream open failed or its already open.
	 */
	bool MP3Streamer::OpenStream(const char* file)
	{
		if (!mpgDll) return 0; // mpg123 not present
		if (FileHandle)  // dont allow reopen an existing stream
			return false;

		FileHandle = mpg_new(nullptr, nullptr);
		mpg_replace_reader_handle(FileHandle, file_read, file_seek, file_close);

		void* iohandle = file_open_ro(file);
		if (!iohandle) {
			indebug(printf("Failed to open file: \"%s\"\n", file));
			return false;
		}
		mpg_open_handle(FileHandle, iohandle);

		int rate, numChannels, encoding;
		if (mpg_getformat(FileHandle, (long*)&rate, &numChannels, &encoding)) {
			indebug(printf("Failed to read mp3 header format: \"%s\"\n", file));
			return false;
		}
		
		int sampleSize = mpg_encsize(encoding);
		// get the actual PCM data size: (NumSamples * NumChannels * SampleSize)
		SampleSize = sampleSize;
		SampleBlockSize = numChannels * sampleSize;
		StreamSize = mpg_length(FileHandle) * SampleBlockSize;
		SampleRate = rate;
		NumChannels = numChannels;
		return true;
	}
	
	/**
	 * Closes the stream and releases all resources held.
	 */
	void MP3Streamer::CloseStream()
	{
		if (!mpgDll) return; // mpg123 not present
		if (FileHandle)
		{
			mpg_close(FileHandle);
			mpg_delete(FileHandle);
			FileHandle = 0;
			StreamSize = 0;
			StreamPos = 0;
			SampleRate = 0;
			NumChannels = 0;
			SampleSize = 0;
			SampleBlockSize = 0;
		}
	}
	
	/**
	 * Reads some Audio data from the underlying stream.
	 * Audio data is decoded into PCM format, suitable for OpenAL.
	 * @param dstBuffer Destination buffer that receives the data
	 * @param dstSize Number of bytes to read
	 * @return Number of bytes read. 0 if stream is uninitialized or end of stream reached.
	 */
	int MP3Streamer::ReadSome(void* dstBuffer, int dstSize)
	{
		if (!mpgDll) return 0; // mpg123 not present
		if (!FileHandle)
			return 0; // nothing to do here
		int count = StreamSize - StreamPos; // calc available data from stream
		if (count == 0) // if stream available bytes 0?
			return 0; // EOS reached
		if (count > dstSize) // if stream has more data than buffer
			count = dstSize; // set bytes to read bigger
		count -= count % SampleBlockSize; // make sure count is aligned to blockSize

		size_t bytesRead;
		mpg_read(FileHandle, (unsigned char*)dstBuffer, count, &bytesRead);
		StreamPos += bytesRead;
		return bytesRead;
	}

	/**
	 * Seeks to the appropriate byte position in the stream.
	 * This value is between: [0...StreamSize]
	 * @param streampos Position in the stream to seek to in bytes
	 * @return The actual position where seeked, or 0 if out of bounds (this also means the stream was reset to 0).
	 */
	unsigned int MP3Streamer::Seek(unsigned int streampos)
	{
		if (!mpgDll) return 0;
		if (int(streampos) >= StreamSize)
			streampos = 0;
		int actual = streampos / SampleBlockSize; // mpg_seek works by sample blocks, so lets select the sample
		mpg_seek(FileHandle, actual, SEEK_SET);
		return streampos;
	}

#pragma endregion







	////////
	//// OGGStreamer impl.
	////

#pragma region OggStreamer

#include "Decoders\Vorbis\vorbisfile.h"

#pragma data_seg("SHARED")
static HMODULE vfDll = NULL;
static int (*oggv_clear)(void* vf) = 0;
static long (*oggv_read)(void* vf, char* buffer, int length, int bigendiannp, int word, int sgned, int* bitstream) = 0;
static long (*oggv_pcm_seek)(void* vf, INT64 pos) = 0;
static UINT64 (*oggv_pcm_tell)(void* vf) = 0;
static UINT64 (*oggv_pcm_total)(void* vf, int i) = 0;
static vorbis_info* (*oggv_info)(void* vf, int link) = 0;
static vorbis_comment* (*oggv_comment)(void* vf, int link) = 0;
static int (*oggv_open_callbacks)(void* datasource, int* vf, char* initial, long ibytes, ov_callbacks cb) = 0;
#pragma data_seg()

static size_t oggv_read_func(void* ptr, size_t size, size_t nmemb, void* handle) {
	return file_read(handle, ptr, size * nmemb);
}
static int oggv_seek_func(void* handle, INT64 offset, int whence) {
	return file_seek(handle, (long)offset, whence); 
}
static int oggv_close_func(void* handle) {
	return file_close(handle); 
}
static long oggv_tell_func(void* handle) {
	return file_tell(handle); 
}

template<class Proc> static inline void LoadVorbisProc(Proc* outProcVar, const char* procName)
{
	*outProcVar = (Proc)GetProcAddress(vfDll, procName);
}
static void _UninitVorbis()
{
	FreeLibrary(vfDll);
	vfDll = NULL;
}
static void _InitVorbis()
{
	static const char* vorbislib = "vorbisfile";
	if (!(vfDll = LoadLibraryA(vorbislib))) // ogg.dll and vorbis.dll is loaded by vorbisfile.dll
	{
		printf("Failed to load DLL %s!\n", vorbislib);
		return;
	}
	LoadVorbisProc(&oggv_clear, "ov_clear");
	LoadVorbisProc(&oggv_read, "ov_read");
	LoadVorbisProc(&oggv_pcm_seek, "ov_pcm_seek");
	LoadVorbisProc(&oggv_pcm_tell, "ov_pcm_tell");
	LoadVorbisProc(&oggv_pcm_total, "ov_pcm_total");
	LoadVorbisProc(&oggv_info, "ov_info");
	LoadVorbisProc(&oggv_comment, "ov_comment");
	LoadVorbisProc(&oggv_open_callbacks, "ov_open_callbacks");
	atexit(_UninitVorbis);
}




	/** 
	 * Creates a new unitialized OGG AudioStreamer.
	 * You should call OpenStream(file) to initialize the stream. 
	 */
	OGGStreamer::OGGStreamer() : AudioStreamer()
	{
		if(!vfDll) _InitVorbis();
	}

	/**
	 * Creates and Initializes a new OGG AudioStreamer.
	 */
	OGGStreamer::OGGStreamer(const char* file) : AudioStreamer()
	{
		if(!vfDll) _InitVorbis();
		OpenStream(file);
	}
	
	/**
	 * Destroys the AudioStream and frees all held resources
	 */
	OGGStreamer::~OGGStreamer()
	{
		CloseStream();
	}

	/**
	 * Opens a new stream for reading.
	 * @param file Audio file to open
	 * @return TRUE if stream is successfully opened and initialized. FALSE if the stream open failed or its already open.
	 */
	bool OGGStreamer::OpenStream(const char* file)
	{
		if (!vfDll) return false; // vorbis not present
		if (FileHandle) // dont allow reopen an existing stream
			return false;

		void* iohandle = file_open_ro(file);
		if (!iohandle) {
			indebug(printf("Failed to open file: \"%s\"\n", file));
			return false;
		}
		ov_callbacks cb = { oggv_read_func, oggv_seek_func, oggv_close_func, oggv_tell_func };
		FileHandle = (int*)malloc(sizeof(OggVorbis_File)); // filehandle is actually Vorbis handle

		if (int err = oggv_open_callbacks(iohandle, FileHandle, NULL, 0, cb)) {
			const char* errmsg;
			switch(err) {
			case OV_EREAD:		errmsg = "Error reading OGG file!";			break;
			case OV_ENOTVORBIS: errmsg = "Not an OGG vorbis file!";			break;
			case OV_EVERSION:	errmsg = "Vorbis version mismatch!";		break;
			case OV_EBADHEADER: errmsg = "Invalid vorbis bitstream header"; break;
			case OV_EFAULT:		errmsg = "Internal logic fault";			break;
			}
			CloseStream();
			indebug(printf("Failed to open OGG file \"%s\": %s\n", file, errmsg));
			return false;
		}
		vorbis_info* info = oggv_info(FileHandle, -1);
		if(!info) {
			CloseStream();
			indebug(printf("Failed to acquire OGG stream format: \"%s\"\n", file));
			return false;
		}
		SampleRate = int(info->rate);
		NumChannels = info->channels;
		SampleSize = 2;						// OGG samples are always 16-bit
		SampleBlockSize = 2 * NumChannels;	// OGG samples are always 16-bit
		StreamSize = (int)oggv_pcm_total(FileHandle, -1) * SampleBlockSize; // streamsize in total bytes
		return true;
	}
	
	/**
	 * Closes the stream and releases all resources held.
	 */
	void OGGStreamer::CloseStream()
	{
		if(!vfDll) return; // vorbis not present
		if(FileHandle)
		{
			oggv_clear(FileHandle);
			free(FileHandle);
			FileHandle = 0;
			StreamSize = 0;
			StreamPos = 0;
			SampleRate = 0;
			NumChannels = 0;
			SampleSize = 0;
			SampleBlockSize = 0;
		}
	}

	/**
	 * Reads some Audio data from the underlying stream.
	 * Audio data is decoded into PCM format, suitable for OpenAL.
	 * @param dstBuffer Destination buffer that receives the data
	 * @param dstSize Number of bytes to read. 64KB is good for streaming (gives ~1.5s of playback sound).
	 * @return Number of bytes read. 0 if stream is uninitialized or end of stream reached before reading.
	 */
	int OGGStreamer::ReadSome(void* dstBuffer, int dstSize)
	{
		if(!vfDll) return 0; // vorbis not present
		if(!FileHandle)
			return 0; // nothing to do here
		int count = StreamSize - StreamPos; // calc available data from stream
		if(count == 0) // if stream available bytes 0?
			return 0; // EOS reached
		if(count > dstSize) // if stream has more data than buffer
			count = dstSize; // set bytes to read bigger
		count -= count % SampleBlockSize; // make sure count is aligned to blockSize

		int current_section;
		int bytesTotal = 0; // total bytes read
		do 
		{
			// TODO: Fix Ogg stream error:
			//       File handles are not shared, so creating 2x of the same stream can fail
			//       Find a way to share the stream or the data
			int bytesRead = oggv_read(FileHandle, (char*)dstBuffer + bytesTotal, 
				(count - bytesTotal), 0, 2, 1, &current_section);

			if (bytesRead == 0) 
				break; // EOF!

			bytesTotal += bytesRead;
		}
		while (bytesTotal < count);

		StreamPos += bytesTotal;
		return bytesTotal;
	}

	/**
	 * Seeks to the appropriate byte position in the stream.
	 * This value is between: [0...StreamSize]
	 * @param streampos Position in the stream to seek to
	 * @return The actual position where seeked, or 0 if out of bounds (this also means the stream was reset to 0).
	 */
	unsigned int OGGStreamer::Seek(unsigned int streampos)
	{
		if (!vfDll) return 0; // vorbis not present
		if (int(streampos) >= StreamSize) streampos = 0; // out of bounds, set to beginning
		
		// TODO: Find the mysterious Ogg stream error
		oggv_pcm_seek(FileHandle, streampos / SampleBlockSize); // seek PCM samples
		return StreamPos = streampos; // finally, update the stream position
	}


#pragma endregion



} // namespace S3D