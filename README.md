record the piano
================

Automatic 24/7 recording and soundcloud upload for my Yamaha N3 Piano.

How it works
------------

A tiny embedded linux device (Odroid-U2) is plugged into a little Behringer USB mixer via USB, which is plugged into the piano using analog audio cables.

When the device boots up, it hops on my wifi network then launches the recordthepiano app, written in C.

The recordthepiano app listens to the audio output from the piano constantly, monitoring for sound. It
automatically starts/stops recording based on whether the piano is making noise.

Recorded audio is encoded in 44100/16/2 FLAC in real-time. As soon as a recording is completed, the recordthepiano 
invokes recordthepiano_upload, a ruby script, that uplaods the FLAC to my soundcloud account:

https://soundcloud.com/blucz

That's it.

Dependencies
------------

- Portaudio v19 dev package
- Ruby v1.9.x with 'soundcloud' gem
- libFlac dev package

Bugs
----

- Right now all the tunables, are hardcoded in recorder.c
- Right now, it looks for my USB audio device by name. This should be done in a less gross way.

