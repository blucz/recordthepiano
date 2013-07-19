package com.blucz.recordthepiano;

import java.util.Locale;

import android.app.ActionBar;
import android.app.FragmentTransaction;
import android.content.ComponentName;
import android.content.Context;
import android.content.ServiceConnection;
import android.graphics.Color;
import android.os.Bundle;
import android.os.Handler;
import android.os.IBinder;
import android.support.v4.app.Fragment;
import android.support.v4.app.FragmentActivity;
import android.support.v4.app.FragmentManager;
import android.support.v4.app.FragmentPagerAdapter;
import android.support.v4.app.NavUtils;
import android.support.v4.view.ViewPager;
import android.util.Log;
import android.view.Gravity;
import android.view.LayoutInflater;
import android.view.Menu;
import android.view.MenuItem;
import android.view.View;
import android.view.ViewGroup;
import android.widget.CompoundButton;
import android.widget.ImageButton;
import android.widget.ProgressBar;
import android.widget.Switch;
import android.widget.TextView;
import android.content.Intent;

public class MainActivity extends FragmentActivity implements ActionBar.TabListener {
    SectionsPagerAdapter mSectionsPagerAdapter;
    ViewPager mViewPager;

    NetworkService service;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        // Set up the action bar.
        final ActionBar actionBar = getActionBar();
        actionBar.setNavigationMode(ActionBar.NAVIGATION_MODE_TABS);

        // Create the adapter that will return a fragment for each of the three
        // primary sections of the app.
        mSectionsPagerAdapter = new SectionsPagerAdapter(getSupportFragmentManager());

        // Set up the ViewPager with the sections adapter.
        mViewPager = (ViewPager) findViewById(R.id.pager);
        mViewPager.setAdapter(mSectionsPagerAdapter);

        mViewPager.setOnPageChangeListener(new ViewPager.SimpleOnPageChangeListener() {
            @Override
            public void onPageSelected(int position) {
                actionBar.setSelectedNavigationItem(position);
            }
        });

        /*
        // For each of the sections in the app, add a tab to the action bar.
        for (int i = 0; i < mSectionsPagerAdapter.getCount(); i++) {
            actionBar.addTab(actionBar.newTab()
                                      .setText(mSectionsPagerAdapter.getPageTitle(i))
                                      .setTabListener(this));
        }
        */

        Intent intent = new Intent(this, NetworkService.class);
        this.bindService(intent, connection, Context.BIND_AUTO_CREATE);
    }

    private ServiceConnection connection = new ServiceConnection() {
        @Override
        public void onServiceConnected(ComponentName componentName, IBinder binder) {
            service = ((NetworkService.NetworkBinder)binder).getService();
        }
        @Override
        public void onServiceDisconnected(ComponentName componentName) { service = null; }
    };


    @Override
    public boolean onOptionsItemSelected(MenuItem item) {
        // Handle item selection
        switch (item.getItemId()) {
            case R.id.action_settings:
                startActivity(new Intent(this, SettingsActivity.class));

            case R.id.action_calibrate:
                if (service != null)
                    service.initialize();

            default:
                return super.onOptionsItemSelected(item);
        }
    }

    @Override
    public boolean onCreateOptionsMenu(Menu menu) {
        getMenuInflater().inflate(R.menu.main, menu);
        return true;
    }
    
    @Override
    public void onTabSelected(ActionBar.Tab tab, FragmentTransaction fragmentTransaction) {
        mViewPager.setCurrentItem(tab.getPosition());
    }

    @Override
    public void onTabUnselected(ActionBar.Tab tab, FragmentTransaction fragmentTransaction) {
    }

    @Override
    public void onTabReselected(ActionBar.Tab tab, FragmentTransaction fragmentTransaction) {
    }

    public class SectionsPagerAdapter extends FragmentPagerAdapter {
        public SectionsPagerAdapter(FragmentManager fm) {
            super(fm);
        }

        @Override
        public Fragment getItem(int position) {
            switch (position) {
                case 0: {
                    Fragment fragment = new ControlsSectionFragment();
                    return fragment;
                }
            }
            return null;
        }

        @Override
        public int getCount() { return 1; }

        @Override
        public CharSequence getPageTitle(int position) {
            Locale l = Locale.getDefault();
            switch (position) {
                case 0: {
                    return getString(R.string.title_controls).toUpperCase(l);
                }
            }
            return null;
        }
    }

    public static class ControlsSectionFragment extends Fragment {
        NetworkService  service;
        View            root_view;
        TextView        time_view;
        TextView        state_view;
        ImageButton     record_button;
        ImageButton     play_button;
        ImageButton     pause_button;
        ImageButton     stop_button;
        ImageButton     discard_button;
        Switch          auto_switch;
        ProgressBar     level_meter;

        public ControlsSectionFragment() {

        }

        private ServiceConnection connection = new ServiceConnection() {
            @Override
            public void onServiceConnected(ComponentName componentName, IBinder binder) {
                service = ((NetworkService.NetworkBinder)binder).getService();
                service.setOnChangedListener(new NetworkService.OnChangedListener() {
                    @Override
                    public void onChanged() {
                        Log.d("ControlsSectionFragment", "IN ON CHANGED");
                        refresh();
                    }
                });
                service.setOnClipListener(new NetworkService.OnClipListener() {
                    @Override
                    public void onClip(int nframes) {
                        if (service == null || root_view == null) return;
                        level_meter.setBackgroundColor(Color.RED);
                        new Handler().postDelayed(new Runnable() {
                            @Override
                            public void run() {
                                level_meter.setBackgroundColor(Color.TRANSPARENT);
                            }
                        }, 500);
                    }
                });
                refresh();
            }
            @Override
            public void onServiceDisconnected(ComponentName componentName) { service = null; }
        };

        void refresh() {
            if (root_view == null) return;

            if (service != null && service.getConnectionState() == NetworkService.CONNECTED) {
                root_view.setEnabled(true);

                double  time        = service.getTime();
                String  record_mode = service.getRecordMode();
                String  state       = service.getState();
                double  level       = service.getLevel();
                double  max_level   = service.getMaxLevel();

                int minutes = (int)time / 60;
                int seconds = (int)time % 60;
                int fraction = (int)(time * 10) % 10;
                time_view.setText(String.format("%02d:%02d.%01d", minutes, seconds, fraction));

                if (state.equals("idle")) {
                    state_view.setText("Idle");
                    record_button.setEnabled(true);
                    play_button.setEnabled(false);
                    pause_button.setEnabled(false);
                    stop_button.setEnabled(false);
                    discard_button.setEnabled(false);
                    play_button.setVisibility(View.GONE);
                    pause_button.setVisibility(View.VISIBLE);
                } else if (state.equals("recording")) {
                    state_view.setText("Recording");
                    record_button.setEnabled(false);
                    play_button.setEnabled(false);
                    pause_button.setEnabled(true);
                    stop_button.setEnabled(true);
                    discard_button.setEnabled(true);
                    play_button.setVisibility(View.GONE);
                    pause_button.setVisibility(View.VISIBLE);
                } else if (state.equals("paused")) {
                    state_view.setText("Paused");
                    record_button.setEnabled(false);
                    play_button.setEnabled(true);
                    pause_button.setEnabled(false);
                    stop_button.setEnabled(true);
                    discard_button.setEnabled(true);
                    play_button.setVisibility(View.VISIBLE);
                    pause_button.setVisibility(View.GONE);
                } else if (state.equals("initializing")) {
                    state_view.setText("Initializing");
                    record_button.setEnabled(false);
                    play_button.setEnabled(false);
                    pause_button.setEnabled(false);
                    stop_button.setEnabled(false);
                    discard_button.setEnabled(false);
                    play_button.setVisibility(View.GONE);
                    pause_button.setVisibility(View.VISIBLE);
                } else {
                    state_view.setText("Unknown");
                    record_button.setEnabled(false);
                    play_button.setEnabled(false);
                    pause_button.setEnabled(false);
                    stop_button.setEnabled(false);
                    discard_button.setEnabled(false);
                    play_button.setVisibility(View.GONE);
                    pause_button.setVisibility(View.VISIBLE);
                }

                auto_switch.setChecked(record_mode.equals("auto"));
                level_meter.setProgress(Math.min((int)(max_level*1000), (int)(level*1000)));
                level_meter.setMax((int)(max_level*1000));
            } else {
                root_view.setEnabled(false);
                record_button.setEnabled(false);
                play_button.setEnabled(false);
                pause_button.setEnabled(false);
                stop_button.setEnabled(false);
                discard_button.setEnabled(false);
                play_button.setVisibility(View.GONE);
                pause_button.setVisibility(View.VISIBLE);
                time_view.setText("00:00.0");
                state_view.setText("Disconnected");
                auto_switch.setChecked(false);
                level_meter.setProgress(0);
                level_meter.setMax(1);
            }
        }

        @Override
        public View onCreateView(LayoutInflater inflater, ViewGroup container, Bundle savedInstanceState) {
            if (service == null) {
                Intent intent = new Intent(this.getActivity(), NetworkService.class);
                this.getActivity().bindService(intent, connection, Context.BIND_AUTO_CREATE);
            }
            if (root_view == null) {
                root_view       = inflater.inflate(R.layout.fragment_controls, container, false);
                time_view       = (TextView)root_view.findViewById(R.id.time_text);
                state_view      = (TextView)root_view.findViewById(R.id.state_text);
                record_button   = (ImageButton)root_view.findViewById(R.id.record_button);
                play_button     = (ImageButton)root_view.findViewById(R.id.play_button);
                pause_button    = (ImageButton)root_view.findViewById(R.id.pause_button);
                stop_button     = (ImageButton)root_view.findViewById(R.id.stop_button);
                discard_button  = (ImageButton)root_view.findViewById(R.id.discard_button);
                auto_switch     = (Switch)root_view.findViewById(R.id.auto_switch);
                level_meter     = (ProgressBar)root_view.findViewById(R.id.level_meter);

                auto_switch.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener() {
                    @Override
                    public void onCheckedChanged(CompoundButton compoundButton, boolean b) {
                        if (service == null) return;
                        if (b) {
                            service.setAutoMode();
                        } else {
                            service.setManualMode();
                        }
                    }
                });

                record_button.setOnClickListener(new View.OnClickListener() {
                    @Override
                    public void onClick(View view) {
                        if (service == null) return;
                        service.record();
                    }
                });

                play_button.setOnClickListener(new View.OnClickListener() {
                    @Override
                    public void onClick(View view) {
                        if (service == null) return;
                        service.unpause();
                    }
                });

                stop_button.setOnClickListener(new View.OnClickListener() {
                    @Override
                    public void onClick(View view) {
                        if (service == null) return;
                        service.stop();
                    }
                });

                discard_button.setOnClickListener(new View.OnClickListener() {
                    @Override
                    public void onClick(View view) {
                        if (service == null) return;
                        service.cancel();
                    }
                });

                pause_button.setOnClickListener(new View.OnClickListener() {
                    @Override
                    public void onClick(View view) {
                        if (service == null) return;
                        service.pause();
                    }
                });

                refresh();
            }
            return root_view;
        }
    }
}
