#define _GNU_SOURCE 

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <stdbool.h>
#include <ctype.h>
#include <time.h>

#include <pthread.h>
#include <sched.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <FLAC/all.h>
#include <portaudio.h>

#include "utils.h"

const char  *DEVICE_NAME                  = "USB Audio CODEC: USB Audio (hw:1,0)";
const int    SAMPLE_RATE                  = 44100;
const int    CHANNELS                     = 2;
const int    FRAMES_PER_BUFFER            = 4410;

const int    BASE_RMS_NBUFFERS            = 20;        // number of buffers of audio to use when determining the 'quiet' audio level at startup
const int    PREROLL_NBUFFERS             = 25;        // number of buffers of pre-roll to keep around 
const double NOISE_THRESHOLD              = 1.3;       // if RMS for a buffer > base_rms * NOISE_THRESHOLD, then it is considered noisy
const int    MIN_RECORDING_LENGTH_SECONDS = 15;

#define      LISTEN_PORT                  (10123)
#define      LISTEN_BACKLOG               (10)
#define      EPOLL_MAX_EVENTS             (10)
#define      MAX_CONNECTIONS              (20)

typedef enum {
    STATE_INITIALIZING,
    STATE_IDLE,
    STATE_RECORDING,
    STATE_PAUSED
} state_t;

const char *state_to_str(state_t state) {
    switch (state) {
        case STATE_INITIALIZING: return "initializing";
        case STATE_IDLE:         return "idle";
        case STATE_RECORDING:    return "recording";
        case STATE_PAUSED:       return "paused";
        default:                 return "unknown";
    }
}

typedef enum {
    RECORD_MODE_AUTO,
    RECORD_MODE_MANUAL
} record_mode_t;

const char *record_mode_to_str(record_mode_t record_mode) {
    switch (record_mode) {
        case RECORD_MODE_AUTO:   return "auto";
        case RECORD_MODE_MANUAL: return "manual";
        default:                 return "unknown";
    }
}

typedef struct {
    record_mode_t       record_mode;
    state_t             state;
    double              level;
    int                 clipped_frames;
} audio_status_t;

typedef enum {
    COMMAND_TYPE_MANUAL,
    COMMAND_TYPE_AUTO,
    COMMAND_TYPE_RECORD,
    COMMAND_TYPE_INITIALIZE,
    COMMAND_TYPE_PAUSE,
    COMMAND_TYPE_UNPAUSE,
    COMMAND_TYPE_STOP,
    COMMAND_TYPE_CANCEL,
} command_type_t;

const char *command_type_to_str(command_type_t type) {
    switch (type) {
        case COMMAND_TYPE_MANUAL: return "manual";
        case COMMAND_TYPE_AUTO: return "auto";
        case COMMAND_TYPE_RECORD: return "record";
        case COMMAND_TYPE_INITIALIZE: return "initialize";
        case COMMAND_TYPE_PAUSE: return "pause";
        case COMMAND_TYPE_UNPAUSE: return "unpause";
        case COMMAND_TYPE_STOP: return "stop";
        case COMMAND_TYPE_CANCEL: return "cancel";
        default: return "unknown";
    }
}

typedef struct {
    command_type_t type;
} command_t;

static audio_status_t DEFAULT_AUDIO_STATUS = {
    .level          = 0.0,
    .record_mode    = RECORD_MODE_AUTO,
    .state          = STATE_INITIALIZING,
    .clipped_frames = 0
};

typedef struct {
    int             sock;
    lineparser_t    lineparser;
} connection_t;

// status pipe is used to communicate status back to the network loop
static int                 status_pipe_read_fd;  
static int                 status_pipe_write_fd;

// control pipe is used to send commands to the audio loop
static int                 control_pipe_read_fd;
static int                 control_pipe_write_fd;

static connection_t        connections[MAX_CONNECTIONS];

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
    int    record_buf_idx = 0;

    // preroll + cached RMS values
    short       rawsamples[FRAMES_PER_BUFFER * CHANNELS];
    FLAC__int32    samples[FRAMES_PER_BUFFER * CHANNELS * PREROLL_NBUFFERS];
    double        past_rms[PREROLL_NBUFFERS];

    // for flac encoder
    FLAC__StreamEncoder *encoder = NULL;
    char tmpfilenamebuf[1024];
    char filenamebuf[1024];
    FILE *file = NULL;

    audio_status_t status = DEFAULT_AUDIO_STATUS;

    if (geteuid() == 0) {
        struct sched_param sparams = {0,};
        sparams.sched_priority = 1;
        int rc = sched_setscheduler(0, SCHED_FIFO, &sparams);
        if (rc == -1) {
            perror("sched_setscheduler");
        } else {
            tracef("set FIFO scheduler");
        }
    }

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
        status.level = rms;

        // warn on clipping
        if (clip > 0) { tracef("%d frames clipped", clip); }
        status.clipped_frames = clip;

        past_rms[preroll_idx] = rms;

        // compute number of loud buffers
        int loud_bufs = 0;
        int idx;
        for (idx = 0; idx < PREROLL_NBUFFERS; idx++) {
            if (past_rms[idx] > base_rms * NOISE_THRESHOLD) loud_bufs++;
        }

        // process pending commands
        bool skip_preroll      = false;
        bool start_recording   = false;
        bool stop_recording    = false;
        bool cancel_recording  = false;

        command_t cmd;
        while (sizeof(cmd) == read(control_pipe_read_fd, &cmd, sizeof(cmd))) {
            tracef("AUDIO GOT CMD %s", command_type_to_str(cmd.type));
            switch (cmd.type) {
                case COMMAND_TYPE_AUTO: {
                    status.record_mode = RECORD_MODE_AUTO;
                } break;

                case COMMAND_TYPE_INITIALIZE: {
                    memset(rawsamples, 0, sizeof(rawsamples));
                    memset(samples, 0, sizeof(rawsamples));
                    memset(past_rms, 0, sizeof(rawsamples));
                    status.state = STATE_INITIALIZING;   
                    buf_idx      = 0;
                    base_rms     = 0;
                    if (status.state == STATE_RECORDING || status.state == STATE_PAUSED) {
                        stop_recording   = true;
                        cancel_recording = true;
                    }
                } break;

                // manual controls
                case COMMAND_TYPE_MANUAL: {
                    status.record_mode = RECORD_MODE_MANUAL;
                } break;

                case COMMAND_TYPE_PAUSE: {
                    status.record_mode = RECORD_MODE_MANUAL;
                     if (status.state == STATE_RECORDING) {
                         status.state = STATE_PAUSED;
                         tracef("paused");
                     } else {
                         tracef("ignored pause when not recording");
                     }
                } break;

                case COMMAND_TYPE_UNPAUSE: {
                    status.record_mode = RECORD_MODE_MANUAL;
                     if (status.state == STATE_PAUSED) {
                         status.state = STATE_RECORDING;
                         tracef("unpaused");
                     } else {
                         tracef("ignored unpause when not paused");
                     }
                } break;

                case COMMAND_TYPE_RECORD: {
                    status.record_mode = RECORD_MODE_MANUAL;
                    skip_preroll    = true;
                    start_recording = true;
                } break;

                case COMMAND_TYPE_STOP: {
                    status.record_mode = RECORD_MODE_MANUAL;
                    if (status.state == STATE_RECORDING || status.state == STATE_PAUSED) {
                        stop_recording = true;
                    }
                } break;

                case COMMAND_TYPE_CANCEL: {
                    status.record_mode = RECORD_MODE_MANUAL;
                    if (status.state == STATE_RECORDING || status.state == STATE_PAUSED) {
                        stop_recording   = true;
                        cancel_recording = true;
                    }
                } break;

                default: break;
            }
        }

        /*
        tracef("in state machine state=%s start_recording=%d stop_recording=%d cancel_recording=%d loud_bufs=%d",
                state_to_str(status.state), start_recording, stop_recording, cancel_recording, loud_bufs);
                */

        // run state machine
        switch (status.state) {
            case STATE_INITIALIZING:
                break;

            case STATE_IDLE:
                if (status.record_mode == RECORD_MODE_AUTO && loud_bufs > (PREROLL_NBUFFERS / 3)) {
                    start_recording = true;
                }
                break;

            case STATE_RECORDING:
                if (status.record_mode == RECORD_MODE_AUTO && loud_bufs == 0) {
                    stop_recording  = true;
                }
                break;

            case STATE_PAUSED:
                break;
        }

        if (start_recording) {
            status.state = STATE_RECORDING;
            if (skip_preroll) {
                record_buf_idx = 0;
            } else {
                record_buf_idx = PREROLL_NBUFFERS;
            }
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

            if (!skip_preroll) {
                // encode the preroll
                for (idx = preroll_idx + 2; idx < preroll_idx + PREROLL_NBUFFERS; idx++) {
                    int realidx           = idx     % PREROLL_NBUFFERS;
                    if (!FLAC__stream_encoder_process_interleaved(encoder, &samples[realidx * FRAMES_PER_BUFFER * CHANNELS], FRAMES_PER_BUFFER)) {
                        tracef("flac encoder process failed");
                        return 1;
                    }
                }
            }
            long long begin_record_end = now_us();
            tracef("started recording in %dms", (int)((begin_record_end - begin_record_start) / 1000));
        }

        if (stop_recording) {
            long long end_record_start = now_us();
            tracef("stop recording (%d loud bufs / %d)", loud_bufs, PREROLL_NBUFFERS);
            int n_seconds = (int)((long long)record_buf_idx * (long long)FRAMES_PER_BUFFER /  (long long)SAMPLE_RATE);
            record_buf_idx = 0;
            status.state = STATE_IDLE;
            FLAC__stream_encoder_finish(encoder);
            FLAC__stream_encoder_delete(encoder);
            encoder = NULL;
            file    = NULL;
            char numbuf[128];
            if (cancel_recording) {
                tracef("discarding recording because user told us to");
                unlink(tmpfilenamebuf);
            } else if (status.record_mode == RECORD_MODE_AUTO && n_seconds < MIN_RECORDING_LENGTH_SECONDS) {
                tracef("discarding recording because too short (%ds < %ds)", n_seconds, MIN_RECORDING_LENGTH_SECONDS);
                unlink(tmpfilenamebuf);
            } else {
                snprintf(numbuf, sizeof(numbuf), ",%ds.flac", n_seconds);
                strcat(filenamebuf, numbuf);
                rename(tmpfilenamebuf, filenamebuf);
            }

            long long end_record_end = now_us();
            tracef("finalized recording in %dms", (int)((end_record_end - end_record_start) / 1000));
        }

        if (status.state == STATE_RECORDING) {
            record_buf_idx++;
            if (!FLAC__stream_encoder_process_interleaved(encoder, &samples[sample_offset], FRAMES_PER_BUFFER)) {
                tracef("flac encoder process failed");
                return 1;
            }
        }

        if (status.state == STATE_INITIALIZING) {
            if (buf_idx < BASE_RMS_NBUFFERS) {
                base_rms += rms;
            }
            if (buf_idx == BASE_RMS_NBUFFERS) {
                base_rms /= BASE_RMS_NBUFFERS;
                tracef("ready to record. Baseline rms = %f", base_rms);
                status.state = STATE_IDLE;
            }
        }

        buf_idx++;

        if (sizeof(status) != write(status_pipe_write_fd, &status, sizeof(status))) {
            failf("short write on status pipe. this shouldn't happen");
        }
    }

    //tracef("got frames rms=%f base=%f", rms, base_rms);

    return 0;
}

static void write_cmd(command_t *cmd) {
    if (sizeof(*cmd) != write(control_pipe_write_fd, cmd, sizeof(*cmd))) {
        failf("short write on command pipe");
    }
}

void ev_endconn(connection_t *conn, bool err) {
    lineparser_destroy(&conn->lineparser);
    shutdown(conn->sock, SHUT_RDWR);
    close(conn->sock);
    conn->sock = 0;
    if (err) {
        tracef("closing socket due to error");
    } else {
        tracef("closing socket due to eof");
    }
}

static void broadcast(const char *buf) {
    int len = strlen(buf);
    int i; 
    for (i = 0; i < MAX_CONNECTIONS; i++) {
        connection_t *conn = &connections[i];
        if (conn->sock != 0) {
            int byteswritten = send(conn->sock, buf, len, 0);
            if (len != byteswritten) {
                ev_endconn(&connections[i], byteswritten < 0);
            }
        }
    }
}

int ev_line(void *userdata, char *line, int len) {
    //connection_t *conn = (connection_t*)userdata;

    tracef("NET GOT '%s'", line);

    while (*line && isspace(*line)) line++;

    if        (strstr(line, "manual") == line) {
        command_t cmd = { .type = COMMAND_TYPE_MANUAL };
        write_cmd(&cmd);
    } else if (strstr(line, "auto") == line) {
        command_t cmd = { .type = COMMAND_TYPE_AUTO };
        write_cmd(&cmd);
    } else if (strstr(line, "record") == line) {
        command_t cmd = { .type = COMMAND_TYPE_RECORD };
        write_cmd(&cmd);
    } else if (strstr(line, "initialize") == line) {
        command_t cmd = { .type = COMMAND_TYPE_INITIALIZE };
        write_cmd(&cmd);
    } else if (strstr(line, "pause") == line) {
        command_t cmd = { .type = COMMAND_TYPE_PAUSE };
        write_cmd(&cmd);
    } else if (strstr(line, "unpause") == line) {
        command_t cmd = { .type = COMMAND_TYPE_UNPAUSE };
        write_cmd(&cmd);
    } else if (strstr(line, "stop") == line) {
        command_t cmd = { .type = COMMAND_TYPE_STOP };
        write_cmd(&cmd);
    } else if (strstr(line, "cancel") == line) {
        command_t cmd = { .type = COMMAND_TYPE_CANCEL };
        write_cmd(&cmd);
    } else {
        tracef("unknown command: '%s'", line);
    }

    return 0;
}

void ev_newconn(connection_t *conn, int conn_sock) {
    conn->sock = conn_sock;
    lineparser_init(&conn->lineparser, ev_line, conn);

    // XXX: send status line
}

void *network_thread_main(void *arg) {
    int listen_sock;
    int one = 1;
    struct sockaddr_in addr = {0,};

    addr.sin_family = AF_INET;
    addr.sin_port = htons((short)LISTEN_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if ((listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        perrorf("socket", "netutil_tcp_listen(%d) failed to socket() ", LISTEN_PORT);
    }
    if (setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, (char *)&one, sizeof(one)) != 0) {
        perrorf("setsockopt", "netutil_tcp_listen(%d) failed to setsockopt() ", LISTEN_PORT);
    }
    if (bind(listen_sock, (struct sockaddr*)&addr, sizeof addr) != 0) {
        perrorf("bind", "netutil_tcp_listen(%d) failed to bind() ", LISTEN_PORT);
    }
    if (listen(listen_sock, LISTEN_BACKLOG) != 0) {
        perrorf("listen", "netutil_tcp_listen(%d) failed to listen() ", LISTEN_PORT);
    }

    int epollfd = epoll_create(10);
    if (epollfd == -1) {
        perrorf("epoll_create", "failed to epoll_create");
    }

    if (0 != ioctl(listen_sock, FIONBIO, (void*)&one)) {
        perrorf("ioctl", "failed to set non-blocking on listen socket");
    }

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = listen_sock;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, listen_sock, &ev) == -1) {
        perrorf("epoll_ctl", "failed to epoll_ctl for listen sock");
    }

    ev.events = EPOLLIN;
    ev.data.fd = status_pipe_read_fd;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, status_pipe_read_fd, &ev) == -1) {
        perrorf("epoll_ctl", "failed to epoll_ctl for status_pipe_read_fd");
    }

    audio_status_t status = DEFAULT_AUDIO_STATUS;
    for (;;) {
        struct epoll_event events[EPOLL_MAX_EVENTS];
        int nfds = epoll_wait(epollfd, events, EPOLL_MAX_EVENTS, -1);
        if (nfds == -1) {
            perrorf("epoll_wait", "failed to epoll_wait");
        }

        int n;
        for (n = 0; n < nfds; ++n) {
            if (events[n].data.fd == status_pipe_read_fd) {
                audio_status_t newstatus = {0,};
                int bytesread = read(status_pipe_read_fd, &newstatus, sizeof(newstatus));
                if (bytesread != sizeof(newstatus)) {
                    failf("short read on status pipe");
                }
                if (newstatus.level       != status.level       ||
                    newstatus.record_mode != status.record_mode ||
                    newstatus.state       != status.state) {
                    ///tracef("got status level=%f record_mode=%s state=%s", newstatus.level, record_mode_to_str(newstatus.record_mode), state_to_str(newstatus.state));
                }

                char buf[1024];
                if (newstatus.level       != status.level) {
                    snprintf(buf, sizeof(buf), "level %f\n", newstatus.level);
                    broadcast(buf);
                    status.level = newstatus.level;
                }
                if (newstatus.record_mode != status.record_mode) {
                    snprintf(buf, sizeof(buf), "mode %s\n", record_mode_to_str(newstatus.record_mode));
                    broadcast(buf);
                    status.record_mode = newstatus.record_mode;
                }
                if (newstatus.state != status.state) {
                    snprintf(buf, sizeof(buf), "state %s\n", state_to_str(newstatus.state));
                    broadcast(buf);
                    status.state = newstatus.state;
                }

                if (newstatus.clipped_frames != 0) {
                    snprintf(buf, sizeof(buf), "clip %d\n", newstatus.clipped_frames);
                    broadcast(buf);
                }

            } else if (events[n].data.fd == listen_sock) {
                struct sockaddr_in local = {0,};
                size_t addrlen = sizeof(local);
                int conn_sock = accept(listen_sock, (struct sockaddr *)&local, &addrlen);
                if (conn_sock == -1) {
                    perrorf("accept", "failed to accept");
                }

                int i;
                int found = 0;
                for (i = 0; i < MAX_CONNECTIONS; i++) {
                    if (connections[i].sock == 0) {
                        if (0 != ioctl(conn_sock, FIONBIO, (void*)&one)) {
                            perrorf("ioctl", "failed to set non-blocking");
                        }
                        ev.events  = EPOLLIN | EPOLLET;
                        ev.data.fd = conn_sock;
                        if (epoll_ctl(epollfd, EPOLL_CTL_ADD, conn_sock, &ev) == -1) {
                            perrorf("epoll_ctl", "epoll_ctl: conn_sock");
                        }

                        if (setsockopt(conn_sock, IPPROTO_TCP, TCP_NODELAY, (char *)&one, sizeof(int)) == -1) {
                            perrorf("setsockopt", "setsockopt nodelay failed");
                        }

                        tracef("accepted new connection");
                        found = 1;
                        ev_newconn(&connections[i], conn_sock);
                        break;
                    }
                }

                if (!found) {
                    tracef("rejecting connection because i am maxed out");
                    shutdown(conn_sock, SHUT_RDWR);
                    close(conn_sock);
                }
            } else {
                int i;
                for (i = 0; i < MAX_CONNECTIONS; i++) {
                    if (connections[i].sock == events[n].data.fd) {
                        char buf[4096];
                        int bytesread = recv(connections[i].sock, buf, sizeof(buf), 0);
                        if (bytesread <= 0) {
                            ev_endconn(&connections[i], bytesread < 0);
                        } else {
                            int off = 0;
                            while (off < bytesread) {
                                int bytesprocessed = lineparser_write(&connections[i].lineparser, buf, off, bytesread - off);
                                off += bytesprocessed;
                            }
                            break;
                        }
                    }
                }
            }
        }
    }
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

    int pipefds[2];

    pipe2(pipefds, O_NONBLOCK);
    status_pipe_read_fd  = pipefds[0];
    status_pipe_write_fd = pipefds[1];

    pipe2(pipefds, O_NONBLOCK);
    control_pipe_read_fd  = pipefds[0];
    control_pipe_write_fd = pipefds[1];

    pthread_t upload_thread;
    pthread_create(&upload_thread, NULL, upload_thread_main, NULL);

    pthread_t network_thread;
    pthread_create(&network_thread, NULL, network_thread_main, NULL);

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

