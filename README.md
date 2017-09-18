# JackAudioPlaybackExample
This is test code to Playback audio using JackAudio http://jackaudio.org/ .
The code reads a wavefile using http://www.mega-nerd.com/libsndfile/  and play back using alsa backend.
The file is playback for a max duration of 180 seconds i.e 3 mins.
The format supported is float and channels.


To compile the code use
gcc -g -o jack_test_play.out jack_test_play.c  -lsndfile  -ljack

This will work for both Jack1 and Jack2

To run the code
./jack_test_play.out -f <file.wav>
