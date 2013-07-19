package com.blucz.recordthepiano;

import android.content.SharedPreferences;
import android.os.Bundle;
import android.app.Activity;
import android.text.Editable;
import android.text.TextWatcher;
import android.view.KeyEvent;
import android.view.Menu;
import android.widget.EditText;
import android.widget.TextView;

public class SettingsActivity extends Activity {

    public static final String PREFS_NAME="RecordThePianoPrefs";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_settings);

        final SharedPreferences settings = getSharedPreferences(PREFS_NAME, 0);

        final EditText editor = (EditText)findViewById(R.id.edit_hostname);
        editor.setText(settings.getString("hostname", "piano"));

        editor.addTextChangedListener(new TextWatcher() {
            @Override
            public void beforeTextChanged(CharSequence charSequence, int i, int i2, int i3) { }

            @Override
            public void onTextChanged(CharSequence charSequence, int i, int i2, int i3) { }

            @Override
            public void afterTextChanged(Editable editable) {
                settings.edit().putString("hostname", editor.getText().toString()).apply();
            }
        });
    }
}
