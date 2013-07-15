#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <portaudio.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <dirent.h>
#include <FLAC/all.h>

const char  *DEVICE_NAME            = "USB Audio CODEC: USB Audio (hw:1,0)";
const int    SAMPLE_RATE            = 44100;
const int    CHANNELS               = 2;
const int    FRAMES_PER_BUFFER      = 8820;

const int    BASE_RMS_NBUFFERS      = 10;        // number of buffers of audio to use when determining the 'quiet' audio level at startup
const int    PREROLL_NBUFFERS       = 15;        // number of buffers of pre-roll to keep around 
const int    RECORD_MIN_NBUFFERS    = 20;        // minimum length of a recording, in buffers
const double NOISE_THRESHOLD        = 1.3;       // if RMS for a buffer > base_rms * NOISE_THRESHOLD, then it is considered noisy

void tracef(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt); 
    fprintf(stderr, "[recordthepiano] ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
}

long long now_us() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000000 + (long long)tv.tv_usec;
}

int run(int device) {
    PaStreamParameters input_params  = {0,};
    input_params.device                    = device;
    input_params.channelCount              = 2;
    input_params.sampleFormat              = paInt16;
    input_params.suggestedLatency          = Pa_GetDeviceInfo(device)->defaultHighInputLatency;
    input_params.hostApiSpecificStreamInfo = NULL;

    int err;

    PaStream *stream;
    err = Pa_OpenStream(&stream, 
                        &input_params, 
                        NULL,
                        SAMPLE_RATE, 
                        FRAMES_PER_BUFFER, 
                        paNoFlag, 
                        NULL, NULL);
    if (err != paNoError) {
        tracef("error initializing stream: %s", Pa_GetErrorText(err));
        return 1;
    }

    err = Pa_StartStream(stream);
    if (err != paNoError) {
        tracef("error starting stream: %s", Pa_GetErrorText(err));
        return 1;
    }

    int    buf_idx        = 0;
    double base_rms       = 0;
    int    ready          = 0;
    int    recording      = 0;
    int    record_buffers = 0;

    // preroll + cached RMS values
    short       rawsamples[FRAMES_PER_BUFFER * CHANNELS];
    FLAC__int32    samples[FRAMES_PER_BUFFER * CHANNELS * PREROLL_NBUFFERS];
    double        past_rms[PREROLL_NBUFFERS];

    // for flac encoder
    FLAC__StreamEncoder *encoder = NULL;
    char tmpfilenamebuf[1024];
    char filenamebuf[1024];
    FILE *file = NULL;

    for (;;) {
        int preroll_idx   = buf_idx % PREROLL_NBUFFERS;
        int sample_offset = FRAMES_PER_BUFFER * CHANNELS * preroll_idx;

        err = Pa_ReadStream(stream, rawsamples, FRAMES_PER_BUFFER);
        if (err == paInputOverflowed) {
            tracef("input overflow");
            continue;
        }
        if (err != paNoError) {
            tracef("error reading stream: %s", Pa_GetErrorText(err));
            return 1;
        }

        // copy short buffer into int32 buffer since this is what libflac wants
        int i;
        for (i = 0; i < FRAMES_PER_BUFFER * CHANNELS; i++) {
            samples[sample_offset + i] = rawsamples[i];
        }

        int clip = 0;

        // compute RMS for this buffer
        double accum = 0;
        int frame, ch;
        for (frame = 0; frame < FRAMES_PER_BUFFER; frame++) {
            for (ch = 0; ch < CHANNELS; ch++) {
                double sample = (double)samples[sample_offset + frame*CHANNELS + ch] / 32768.0;
                accum += sample*sample;
                if (sample > 0.99) clip++;
            }
        }
        double rms = sqrt(accum / (FRAMES_PER_BUFFER * CHANNELS));

        if (clip > 0) {
            tracef("%d frames clipped", clip);
        }

        past_rms[preroll_idx] = rms;

        if (ready) {
            int loud_bufs = 0;
            int idx;
            for (idx = 0; idx < PREROLL_NBUFFERS; idx++) {
                if (past_rms[idx] > base_rms * NOISE_THRESHOLD) loud_bufs++;
            }

            //tracef("loud bufs %d", loud_bufs);

            if (loud_bufs > (PREROLL_NBUFFERS / 3) && !recording) {
                recording = 1;
                record_buffers = PREROLL_NBUFFERS;
                long long begin_record_start = now_us();
                tracef("start recording (%d loud bufs / %d)", loud_bufs, PREROLL_NBUFFERS);

                struct tm start_time;
                time_t tt = time(NULL);
                localtime_r(&tt, &start_time);
                strftime(filenamebuf, sizeof(filenamebuf), "%Y-%m-%dT%H:%M:%S%z", &start_time);
                strftime(tmpfilenamebuf, sizeof(filenamebuf), "%Y-%m-%dT%H:%M:%S%z", &start_time);
                strcat(tmpfilenamebuf, ".flac.tmp");

                file = fopen(tmpfilenamebuf, "wb");
                if (file == NULL) {
                    tracef("couldn't open file");
                    return 1;
                }

                encoder = FLAC__stream_encoder_new();
                if (encoder == NULL) {
                    tracef("couldn't start flac encoder");
                    return 1;
                }

                FLAC__stream_encoder_set_channels(encoder, 2);
                FLAC__stream_encoder_set_bits_per_sample(encoder, 16);
                FLAC__stream_encoder_set_sample_rate(encoder, 44100);

                FLAC__StreamEncoderInitStatus initstatus = FLAC__stream_encoder_init_FILE(encoder, file, NULL, NULL);
                if (initstatus != FLAC__STREAM_ENCODER_INIT_STATUS_OK) {
                    tracef("couldn't init flac encoder");
                    return 1;
                }

                // encode the preroll
                for (idx = preroll_idx + 2; idx < preroll_idx + PREROLL_NBUFFERS; idx++) {
                    int realidx           = idx     % PREROLL_NBUFFERS;
                    if (!FLAC__stream_encoder_process_interleaved(encoder, &samples[realidx * FRAMES_PER_BUFFER * CHANNELS], FRAMES_PER_BUFFER)) {
                        tracef("flac encoder process failed");
                        return 1;
                    }
                }
                long long begin_record_end = now_us();
                tracef("started recording in %dms", (int)((begin_record_end - begin_record_start) / 1000));
            }
            if (loud_bufs == 0 && recording && record_buffers > RECORD_MIN_NBUFFERS) {
                long long end_record_start = now_us();
                tracef("stop recording (%d loud bufs / %d)", loud_bufs, PREROLL_NBUFFERS);
                int n_seconds = (int)((long long)record_buffers * (long long)FRAMES_PER_BUFFER /  (long long)SAMPLE_RATE);
                recording = 0;
                record_buffers = 0;
                FLAC__stream_encoder_finish(encoder);
                FLAC__stream_encoder_delete(encoder);
                encoder = NULL;
                file    = NULL;
                char numbuf[128];
                snprintf(numbuf, sizeof(numbuf), ",%ds.flac", n_seconds);
                strcat(filenamebuf, numbuf);
                rename(tmpfilenamebuf, filenamebuf);

                long long end_record_end = now_us();
                tracef("finalized recording in %dms", (int)((end_record_end - end_record_start) / 1000));
            }

            if (recording) {
                record_buffers++;
                if (!FLAC__stream_encoder_process_interleaved(encoder, &samples[sample_offset], FRAMES_PER_BUFFER)) {
                    tracef("flac encoder process failed");
                    return 1;
                }
            }
        }

        if (buf_idx < BASE_RMS_NBUFFERS) {
            base_rms += rms;
        }
        if (buf_idx++ == BASE_RMS_NBUFFERS && !ready) {
            base_rms /= BASE_RMS_NBUFFERS;
            tracef("ready to record. Baseline rms = %f", base_rms);
            ready = 1;
        }

        //tracef("got frames rms=%f base=%f", rms, base_rms);
    }

    return 0;
}

void *upload_thread_main(void *arg) {
    // upload flac files
top:
    for (;;) {
        DIR *dir = opendir(".");
        struct dirent *ent;
        for (ent = readdir(dir); ent; ent = readdir(dir)) {
            const char *filename = ent->d_name;
            if (strcmp(filename + strlen(filename) - 5, ".flac")) continue;         // only upload flacs
            long long upload_start = now_us();
            tracef("uploading %s to soundcloud", filename);
            char cmdbuf[4096];
            snprintf(cmdbuf, sizeof(cmdbuf),  "recordthepiano_upload '%s'", filename);
            int rc = system(cmdbuf);
            long long upload_end = now_us();
            if (rc == 0) {
                tracef("uploaded succeeded in %dms", (int)((upload_end - upload_start) / 1000));
                unlink(filename);
                closedir(dir);
                goto top;
            } else {
                tracef("uploaded failed in %dms", (int)((upload_end - upload_start) / 1000));
            }
        }
        closedir(dir);
        sleep(1);
    }
}

int main(int argc, char **argv) {
    int err = Pa_Initialize();
    if (err != paNoError) {
        tracef("error initializing portaudio: %s", Pa_GetErrorText(err));
        return 1;
    }

    // delete old tmp files
    system("rm -f *.flac.tmp");

    pthread_t upload_thread;
    pthread_create(&upload_thread, NULL, upload_thread_main, NULL);

    setlinebuf(stderr);

    int ndevices = Pa_GetDeviceCount();
    int device;
    /*
    tracef("detecting devices");
    for (device = 0; device < ndevices; device++) {
        const PaDeviceInfo *info = Pa_GetDeviceInfo(device);
        tracef("    device %d", device);
        tracef("        name: %s", info->name);
        tracef("        inch: %d", info->maxInputChannels);
        tracef("       outch: %d", info->maxOutputChannels);
        tracef("       srate: %f", info->defaultSampleRate);
    }
    */

    for (device = 0; device < ndevices; device++) {
        const PaDeviceInfo *info = Pa_GetDeviceInfo(device);
        if (!strcmp(info->name, DEVICE_NAME)) {
            return run(device);
        }
    }
    printf("Device '%s' not found. Exiting", DEVICE_NAME);

    return 1;
}

