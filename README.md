record the piano
================

Automatic 24/7 recording and soundcloud upload for my Yamaha N3 Piano.

How it works
------------

A tiny embedded linux device (Odroid-U2) is plugged into a Behringer USB mixer, which is plugged into the piano using analog audio cables.

When the device boots up, it hops on my wifi network then launches the recordthepiano app, written in C.

The recordthepiano app listens to the audio output from the piano constantly, monitoring for sound. It
automatically starts/stops recording based on whether the piano is making noise.

An android app allows for additional remote control and feedback for the recording process. I have dedicated a nexus 7 to this purpose. 

Recorded audio is encoded in 44100/16/2 FLAC in real-time. As soon as a recording is completed, the recordthepiano 
app invokes recordthepiano_upload, a ruby script, that uploads the FLAC to my soundcloud account:

https://soundcloud.com/blucz

That's it.

Photos  
------

Remote control in its dock

![remote](https://raw.github.com/blucz/recordthepiano/master/images/remote.jpg)

The recorder hardware

![box](https://raw.github.com/blucz/recordthepiano/master/images/box.jpg)

The recorder hardware's guts

![guts](https://raw.github.com/blucz/recordthepiano/master/images/guts.jpg)

Screenshot of remote control

![screenshot](https://raw.github.com/blucz/recordthepiano/master/images/screenshot.png)

Dependencies
------------

- Portaudio v19 dev package
- Ruby v1.9.x with 'soundcloud' gem
- libFlac dev package

Network Protocol
----------------

recordthepiano can be controlled by a network protocol. 

It listens to TCP port 10123. The protocol is line-based. Lines are terminated by '\n'. You can play with the protocol
by using nc to connect to the port.

Clients send commands to recordthepiano + receive status messages.

Upon accepting a new connection, recordthepiano sends all of the status messages to the client to ensure that it 
has correct initial values.

Commands:

    auto        - switch into automatic recording mode 
    manual      - switch into manual recording mode 
    stop        - finish recording + upload to soundcloud
    cancel      - finish recording + discard
    pause       - pause recording
    unpause     - unpause recording
    initialize  - cancel any current recording and re-calibrate base noise level

Status messages:

    base_level <rms level>      - calibrated base noise level  (rms ranges from [0,0.5])
    level <rms level>           - noise level of last 0.1s buffer (rms ranges from [0,0.5])
    state <state>               - the current state (idle,recording,paused,initializing)
    mode <mode>                 - the current record mode (audo,manual)
    clip <nframes>              - that <nframes> frames have clipped

Bugs
----

- Right now all the tunables are hardcoded in recorder.c
- Right now, it looks for my USB audio device by name. This should be done in a less gross way.

