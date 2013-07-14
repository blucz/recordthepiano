recordthepiano
==============

24/7 automatic recording, flac encode, upload, and listening ui for Yamaha N3 Piano.

An embedded ARM system records the aux-out of the piano constantly, keeping a few seconds of
preroll in memory for noise detection. When the piano starts making noise, the recording software
begins encoding the audio into a flac file in local flash memory. When the piano stops making 
noise for a few seconds, the flac file is uploaded to the server.

