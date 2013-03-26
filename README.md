Sound3D
=======

An open source 3D Audio Library built upon OpenAL, supports *.wav, *.mp3, *.ogg file streaming, 
shared buffers or streams between different sound objects and has a simple, yet well documented API.

You can easily buffer or stream sound files and play them as simple audio or 3D positional audio.
Since this library is essentially a wrapper with added decoders, most OpenAL features are supported,
or will be in the future.

Currently supported features:
	- WAV loading/streaming
	- MP3 loading/streaming
	- OGG loading/streaming
	- 3D positional audio (Sound3D class)
	- simple audio (Sound class)
	- static sound buffers (SoundBuffer class)
	- dynamic sound streams (SoundStream class)

Planned features:
	- EAX effects support
	- Doppler effect / sound speed OpenAL interface

Todo:
	- Finish sound cone documentation
	- Find mysterious OGG stream crash when too many SoundObjects stream from a single OggStream

Dependencies: (just copy these to your executable directory)
	- libmpg123.dll (required for MP3 loading, if not found, all MP3 loads will fail)
	- vorbisfile.dll, vorbis.dll, ogg.dll (all 3 required for OGG loading, if not found, all OGG loads will fail)


How to get started? - Compile and run "sample.cpp", it contains everything you need to get started.
