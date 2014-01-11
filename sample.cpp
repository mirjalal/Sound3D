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
using namespace S3D;
#include <stdlib.h>
#include <Windows.h>
#include <vector>
#include <time.h>

bool keyDown(int vkey);										// TRUE if windows virtual key was down
void updateStats(int numSounds, float volume, int loop);	// updates status on console title

int main()
{
	// Sound3D - An open source 3D Audio Library
	// This example serves to introduce the basic concepts of this library
	// Supported audio formats are: WAV, MP3 and OGG

	SoundBuffer* buffers[5] = { 0 };

	buffers[0] = new SoundBuffer();			// This is a static SoundBuffer
	buffers[0]->Load("explosion.ogg");		// The entire ogg file is loaded
											// SoundBuffers are good for SMALL sound Effects

	buffers[1] = new SoundStream();			// This is a SoundStream, the audio is streamed and not loaded whole
	buffers[1]->Load("thunder.ogg");		// Each streamed chunk is exactly 1s of audio data
											// SoundStreams are good for LARGE music or ambient files

	buffers[2] = new SoundStream("forest.ogg");			// soundstreams are auto-streamed in a separate thread
	buffers[3] = new SoundBuffer("crowdcheer.wav");		// also support WAV files
	buffers[4] = new SoundStream("balls_of_fire.mp3");	// and MP3 files


	printf("Controls:\n");
	printf("1 - Create Explosion     (OGG Buffer)\n");
	printf("2 - Create Thunder       (OGG Stream)\n");
	printf("3 - Create Forest        (OGG Stream)\n");
	printf("4 - Create Cheer         (WAV Buffer)\n");
	printf("5 - Create Balls of Fire (MP3 Stream)\n");
	printf("L - Toggle looping\n");
	printf("S - Stop all sounds\n");
	printf("UP/DOWN - Inc/Dec volume\n");
	printf("ESC - exit\n");
	
	std::vector<Sound*> activeSounds; // we'll use it to keep track of generated sounds
	clock_t start1 = clock(); // to limit stream creation time
	clock_t start2 = clock();
	clock_t start3 = clock();
	bool looping = false;

	while (true)
	{
		if (keyDown('1')) // most minimalistic example
		{
			activeSounds.push_back(new Sound(buffers[0], looping, true)); // sound:buffer1, loop:false, play:true
		}
		else if (keyDown('2') && (clock()-start1) >= 1000)
		{
			// soundobjects can be initialized with a buffer and played later when needed
			Sound* sound = new Sound(buffers[1]);	// sound:stream1
			sound->Play();							// start playing the sound
			activeSounds.push_back(sound);
			start1 = clock();
		}
		else if (keyDown('3') && (clock()-start2) >= 1000) 
		{
			// soundobjects can be empty and forced to play a new soundbuffer/stream later
			Sound* sound = new Sound();				// just an empty soundobject
			sound->Play(buffers[2], false);			// sound:buffers[2], loop:false
			activeSounds.push_back(sound);
			start2 = clock();
		}
		else if (keyDown('4'))
		{
			activeSounds.push_back(new Sound(buffers[3], looping, true));
		}
		else if (keyDown('5') && (clock()-start3) >= 1000)
		{
			activeSounds.push_back(new Sound(buffers[4], looping, true));
			start3 = clock();
		}
		else if (keyDown('L')) // toggle looping mode
		{
			looping = !looping;
		}
		else if (keyDown('S')) // stop playing all sounds
		{
			
			for (int i = 0; i < 10; ++i) // smooth fadeout over 200ms:
			{
				for (Sound* sound : activeSounds) // gradually reduce all sounds by 0.05f
					if (sound->Volume() >= 0.1f)
						sound->Volume(sound->Volume() - 0.1f);
				Sleep(20); // 10x20ms = 200ms
			}

			// now delete everything:
			for(Sound* sound : activeSounds)
				delete sound; // sound is auto-stopped
			activeSounds.clear();
		}
		else if (keyDown(VK_UP))
		{
			// there are two ways to change sound volume
			// #1 - Change the volume of a specific sound
			//      This value has limited range: [0.0 - 1.0]
			//for(Sound* sound : activeSounds)
			//	sound->Volume(sound->Volume() + 0.1f);

			// #2 - Change the gain of the global listener
			//      This value has no limited range: [0.0 - Any], the software must enforce its own bounds on this
			//      because the sound will easily start distorting beyond 2.0
			Listener::Volume(Listener::Volume() + 0.1f);
		}
		else if (keyDown(VK_DOWN))
		{
			//for(Sound* sound : activeSounds)
			//	sound->Volume(sound->Volume() - 0.1f);
			Listener::Volume(Listener::Volume() - 0.1f);
		}
		else if (keyDown(VK_ESCAPE))
			break;

		// Check for any finished sounds:
		for (size_t i = 0; i < activeSounds.size(); )
		{
			if(activeSounds[i]->IsPlaying() == false) // it stopped playing?
			{
				delete activeSounds[i];
				activeSounds.erase(activeSounds.begin() + i);
				continue;
			}
			++i;
		}

		updateStats(activeSounds.size(), Listener::Volume(), looping);

		Sleep(50); // sleep a bit..
	}

	for (Sound* sound : activeSounds)
		delete sound; // sound is automatically stopped upon deletion
	activeSounds.clear();

	for (SoundBuffer* buffer : buffers)
		delete buffer;
	
	return 0;
}

bool keyDown(int vkey) // TRUE if windows virtual key was pressed
{
	if (GetAsyncKeyState(vkey) & 0x0001) // is the least significant bit set?
		return true; // the vkey was pressed after previous call to GetAsyncKeyState
	return false;
}

void updateStats(int numSounds, float volume, int loop)
{
	static int NumSounds = -1; // set these to invalid values at first
	static float Volume = -1.0f;
	static int Loop = -1;

	if (NumSounds != numSounds || Volume != volume || Loop != loop)
	{
		char buffer[80];
		sprintf(buffer, "S3D - ActiveSounds: %d   Volume: %.1f   Loop: %s", 
				numSounds, volume, loop ? "true" : "false");
		SetConsoleTitleA(buffer);
		NumSounds = numSounds;
		Volume = volume;
	}
}