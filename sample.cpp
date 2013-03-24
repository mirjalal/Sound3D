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
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <vector>
#include <algorithm> // remove_if
#include <time.h>

bool keyPress(int vkey) // TRUE if windows virtual key was pressed
{
	if(GetAsyncKeyState(vkey) & 0x0001) // is the least significant bit set?
		return true; // the vkey was pressed after previous call to GetAsyncKeyState
	return false;
}

int main()
{
	// Sound3d - An open source 3D Audio Library
	// This example serves to introduce the basic concepts of this library
	// Supported audio formats are: WAV, MP3 and OGG

	SoundBuffer* buffer1 = new SoundBuffer(); // this is a static soundbuffer
	buffer1->Load("explosion_01.ogg"); // the entire ogg file is loaded

	SoundStream* stream1 = new SoundStream(); // this is a soundstream, the audio is obviously streamed and not loaded whole
	stream1->Load("buddhist_01"); // buddhist_01.ogg, OGG format is auto-detected

	ManagedSoundStream* stream2 = new ManagedSoundStream(); // managed soundstreams are auto-streamed in a separate thread
	stream2->Load("ambient_forest.ogg");

	printf("Controls:\n");
	printf("1 - Create Explosion\n");
	printf("2 - Create Buddhist\n");
	printf("3 - Create Forest\n");
	printf("S - Stop all sounds\n");
	printf("ESC - exit\n");
	
	std::vector<Sound*> activeSounds; // we'll use it to keep track of generated sounds
	clock_t start = clock(); // use this to limit stream soundobject creation

	while(true)
	{
		// there are two ways to stream
		// #1 - Check individual sounds to stream
		for(Sound* sound : activeSounds) // go through all active sounds
		{
			if(sound->IsStreamable()) // if the sound is streamable
				sound->Stream(); // Data is only streamed if the previous soundbuffer in the queue was processed, so this method can be called many times.
		}

		// #2 - Check the specific SoundStreams to stream all bound Sounds
		stream1->Stream();



		// check for keypresses
		if(keyPress('1')) // most minimalistic example
		{
			activeSounds.push_back(new Sound(buffer1, false, true)); // sound:buffer1, loop:false, play:true
		}
		else if(keyPress('2') && (clock()-start) >= 1000)
		{
			// soundobjects can be initialized with a buffer and played later when needed
			Sound* sound = new Sound(stream1); // sound:stream1
			sound->Play(); // start playing the sound
			activeSounds.push_back(sound);
			start = clock();
		}
		else if(keyPress('3') && (clock()-start) >= 1000) 
		{
			// soundobjects can be empty and forced to play a new soundbuffer/stream later
			Sound* sound = new Sound(); // just an empty soundobject
			sound->Play(stream2, false); // sound:stream2, loop:false
			activeSounds.push_back(sound);
			start = clock();
		}
		else if(keyPress('S')) // stop playing all sounds
		{
			for(Sound* sound : activeSounds)
				delete sound; // sound is auto-stopped
			activeSounds.clear();
		}
		else if(keyPress(VK_ESCAPE))
			break;


		// Check for any finished sounds:
		for(unsigned i = 0; i < activeSounds.size(); )
		{
			if(activeSounds[i]->IsPlaying() == false) // it stopped playing?
			{
				delete activeSounds[i];
				activeSounds.erase(activeSounds.begin() + i);
				continue;
			}
			++i;
		}


		Sleep(50); // sleep a bit..
	}

	for(auto it = activeSounds.begin(); it != activeSounds.end(); ++it)
		delete *it; // sound is automatically stopped upon deletion
	delete buffer1;
	delete stream1;
	delete stream2;

	return 0;
}


