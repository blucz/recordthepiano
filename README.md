recordthepiano
==============

24/7 automatic recording, flac encode, upload, and listening ui for Yamaha N3 Piano.

A tiny embedded linux device (Odroid-U2) is plugged into a little Behringer USB mixer via USB, which is plugged into the piano using analog audio cables.

When the device boots up, it hops on my wifi network then launches the recordthepiano app, written in C.

The recordthepiano app listens to the audio output from the piano constantly, monitoring for noise. It
automatically starts/stops recording based on whether the piano is in use. The noise floor is determined experimentally
at startup in terms of the RMS of the signal. If the RMS exceeds 1.3x the base RMS for more than 30% of the 0.2s periods
within a 3s preroll buffer, then recording starts from the beginning of the preroll. If there is no noise for 3s, 
recording stops.

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

