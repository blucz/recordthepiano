package com.blucz.recordthepiano;

import android.app.IntentService;
import android.app.Service;
import android.content.Intent;
import android.content.SharedPreferences;
import android.os.AsyncTask;
import android.os.Binder;
import android.os.Handler;
import android.os.IBinder;
import android.os.IInterface;
import android.os.Parcel;
import android.os.RemoteException;
import android.util.Log;

import java.io.BufferedReader;
import java.io.FileDescriptor;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.net.InetAddress;
import java.net.Socket;
import java.net.UnknownHostException;
import java.io.*;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.TimeUnit;

/**
 * Created by brian on 7/18/13.
 */
public class NetworkService extends Service {
    public static final String PREFS_NAME="RecordThePianoPrefs";
    public static final String TAG = "NetworkService";

    public static final int DISCONNECTED = 0;
    public static final int CONNECTING = 1;
    public static final int CONNECTED  = 2;
    public static final int CONNECTION_FAILED = 3;

    String state = "";
    String recordmode = "";
    double time = 0;
    double level = 0;
    double base_level = 0;
    OnChangedListener _onchanged;
    OnClipListener _onclip;
    int connectionstate = DISCONNECTED;
    int connection_seq;
    Socket current_socket;
    final ExecutorService threadpool = Executors.newFixedThreadPool(1);
    Handler cx;

    private final IBinder binder = new NetworkBinder();

    class NetworkBinder extends Binder {
        NetworkService getService() {
            return NetworkService.this;
        }
    }

    interface OnClipListener {
        void onClip(int nframes);
    }

    interface OnChangedListener {
        void onChanged();
    }

    @Override
    public void onCreate() {
        Log.d(TAG, "creating networkservice");
        cx = new Handler();
        super.onCreate();
        final SharedPreferences settings = getSharedPreferences(PREFS_NAME, 0);
        settings.registerOnSharedPreferenceChangeListener(new SharedPreferences.OnSharedPreferenceChangeListener() {
            @Override
            public void onSharedPreferenceChanged(SharedPreferences sharedPreferences, String s) {
                if (s == "hostname") {
                    disconnect();
                    connect();
                }
            }
        });

        final ScheduledExecutorService scheduler = Executors.newSingleThreadScheduledExecutor();
        scheduler.scheduleAtFixedRate(new Runnable() {
            public void run() {
                ensureConnected();
            }
        }, 0, 2, TimeUnit.SECONDS);
        ensureConnected();
        Log.d(TAG, "done creating networkservice");

    }

    @Override
    public IBinder onBind(Intent intent) {
        return binder;
    }

    public void ensureConnected() {
        cx.post(new Runnable() {
            public void run() {
                if (connectionstate != CONNECTED && connectionstate != CONNECTING) {
                    disconnect();
                    connect();
                }
            }
        });
    }

    public void disconnect() {
        Log.d(TAG, "in disconnect()");
        if (connectionstate != DISCONNECTED) {
            connectionstate = DISCONNECTED;
            connection_seq++;
        }
        if (current_socket != null) {
            try {
                current_socket.close();
            } catch (IOException e) {
                current_socket = null;
            }
            onChanged();
        }
    }

    public void updateConnectionState(final int seq, final Socket socket, final int state) {
        cx.post(new Runnable() {
            public void run() {
            if (seq != connection_seq) return;
            connectionstate = state;
            current_socket = socket;
            onChanged();
            }
        });
    }

    public void connect() {
        Log.d(TAG, "in connect()");
        disconnect();
        final int seq = ++connection_seq;
        final SharedPreferences settings = getSharedPreferences(PREFS_NAME, 0);
        final String hostname = settings.getString("hostname", null);
        if (hostname == null) {
            connectionstate = DISCONNECTED;
            return;
        }
        connectionstate = CONNECTING;
        onChanged();
        Thread thread = new Thread() {
            public void run() {
                try {
                    Log.d(TAG, "connecting...");
                    final Socket socket = new Socket(hostname.trim(), 10123);
                    try {
                        updateConnectionState(seq, socket, CONNECTED);
                        Log.d(TAG, "connected");

                        BufferedReader input = new BufferedReader(new InputStreamReader(socket.getInputStream()));

                        while (true) {
                            String line = input.readLine();
                            if (line == null) break;
                            line = line.trim();

                            Log.d(TAG, "NETWORK GOT " + line);

                            if (line.startsWith("level")) {
                                double level = Double.parseDouble(line.substring("level".length()).trim());
                                setLevel(seq, level);
                            } else if (line.startsWith("base_level")) {
                                double base_level = Double.parseDouble(line.substring("base_level".length()).trim());
                                setBaseLevel(seq, base_level);
                            } else if (line.startsWith("time")) {
                                double time = Double.parseDouble(line.substring("time".length()).trim());
                                setTime(seq, time);
                            } else if (line.startsWith("state")) {
                                setState(seq, line.substring("state".length()).trim());

                            } else if (line.startsWith("mode")) {
                                setMode(seq, line.substring("mode".length()).trim());

                            } else if (line.startsWith("clip")) {
                                onClip(seq, Integer.parseInt(line.substring("clip".length()).trim()));

                            } else {
                                Log.d(TAG, "unrecognized input line: '" + line + "'");
                            }
                        }
                    } finally {
                        socket.close();
                        updateConnectionState(seq, socket, DISCONNECTED);
                    }

                } catch (UnknownHostException e) {
                    Log.e(TAG, "unknown host " + hostname);
                    updateConnectionState(seq, null, CONNECTION_FAILED);

                } catch (IOException e) {
                    Log.e(TAG, "io exception when contacting " + hostname + ": " + e);
                    e.printStackTrace();
                    updateConnectionState(seq, null, CONNECTION_FAILED);
                }
            }
        };
        thread.start();
    }

    private void writeCommand(final String cmd) {
        final Socket sock = current_socket;
        if (sock != null) {
            Log.d(TAG, "NETWORK SENT " + cmd);
            threadpool.execute(new Runnable() {
                public void run() {
                    try {
                        sock.getOutputStream().write((cmd.trim()+ "\n").getBytes("UTF8"));
                    } catch (IOException e) {
                        Log.d(TAG, "write failed");
                    }
                }
            });
        }
    }

    private void setMode(final int seq, final String value) {
        cx.post(new Runnable() { public void run() {
            if (seq != connection_seq) return;
            recordmode = value;
            onChanged();
        } });
    }

    private void setState(final int seq, final String value) {
        cx.post(new Runnable() { public void run() {
            if (seq != connection_seq) return;
            state = value;
            onChanged();
        } });
    }

    private void setTime(final int seq, final double value) {
        cx.post(new Runnable() { public void run() {
            if (seq != connection_seq) return;
            time = value;
            onChanged();
        } });
    }

    private void setLevel(final int seq, final double value) {
        cx.post(new Runnable() { public void run() {
            if (seq != connection_seq) return;
            level = value;
            onChanged();
        } });
    }

    private void setBaseLevel(final int seq, final double value) {
        cx.post(new Runnable() { public void run() {
            if (seq != connection_seq) return;
            base_level = value;
            onChanged();
        } });
    }

    private void onClip(final int seq, final int clip) {
        cx.post(new Runnable() { public void run() {
            if (seq != connection_seq) return;
            onClip(clip);
        } });
    }

    public void onClip(int clip) {
        if (_onclip != null) _onclip.onClip(clip);
    }

    public void onChanged() {
        if (_onchanged != null) _onchanged.onChanged();
    }

    public void setOnChangedListener(OnChangedListener l) {
        _onchanged = l;
    }

    public void setOnClipListener(OnClipListener l) {
        _onclip = l;
    }
    public String getState() { return state; }
    public String getRecordMode() { return recordmode; }
    public double getTime() { return time; }
    public int getConnectionState() { return connectionstate; }
    public double getLevel() { return level; }
    public double getBaseLevel() { return base_level; }
    public double getMaxLevel() {  return 0.5; }

    public void unpause() {
        writeCommand("unpause");
    }

    public void pause() {
        writeCommand("pause");
    }

    public void record() {
        writeCommand("record");
    }

    public void stop() {
        writeCommand("stop");
    }

    public void cancel() {
        writeCommand("cancel");
    }

    public void setAutoMode() {
        writeCommand("auto");
    }

    public void setManualMode() {
        writeCommand("manual");
    }

    public void initialize() {
        writeCommand("initialize");
    }

    @Override
    public void onDestroy() {
        disconnect();
    }
}
