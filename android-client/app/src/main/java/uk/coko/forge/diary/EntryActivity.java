package uk.coko.forge.diary;

import android.app.Activity;
import android.content.SharedPreferences;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.Editable;
import android.text.TextWatcher;
import android.widget.EditText;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public class EntryActivity extends Activity {

    private static final int AUTOSAVE_DELAY_MS = 2000;

    private final ExecutorService executor = Executors.newSingleThreadExecutor();
    private final Handler         handler  = new Handler(Looper.getMainLooper());
    private final Runnable        saveTask = this::save;

    private DiaryConnection conn;
    private EditText        editText;
    private int             entryId;
    private long            entryTs;
    private boolean         saving = false;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_entry);

        entryId = getIntent().getIntExtra(MainActivity.EXTRA_ENTRY_ID, -1);
        entryTs = getIntent().getLongExtra(MainActivity.EXTRA_ENTRY_TS, System.currentTimeMillis() / 1000);
        String initialText = getIntent().getStringExtra(MainActivity.EXTRA_ENTRY_TEXT);

        String date = new SimpleDateFormat("yyyy-MM-dd HH:mm", Locale.getDefault())
                          .format(new Date(entryTs * 1000));
        getActionBar().setTitle(entryId > 0 ? "Entry #" + entryId + " — " + date : "New entry");
        getActionBar().setDisplayHomeAsUpEnabled(true);

        editText = findViewById(R.id.edit_content);
        if (initialText != null) editText.setText(initialText);

        editText.addTextChangedListener(new TextWatcher() {
            public void beforeTextChanged(CharSequence s, int start, int count, int after) {}
            public void onTextChanged(CharSequence s, int start, int before, int count) {}
            public void afterTextChanged(Editable s) {
                handler.removeCallbacks(saveTask);
                handler.postDelayed(saveTask, AUTOSAVE_DELAY_MS);
                setStatus("");
            }
        });

        setupConnection();
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        handler.removeCallbacks(saveTask);
        executor.execute(() -> { if (conn != null) conn.disconnect(); });
        executor.shutdown();
    }

    @Override
    public boolean onNavigateUp() {
        handler.removeCallbacks(saveTask);
        if (!saving) save();
        finish();
        return true;
    }

    private void setupConnection() {
        SharedPreferences prefs = getSharedPreferences(SetupActivity.PREFS, MODE_PRIVATE);
        String host    = prefs.getString(SetupActivity.KEY_HOST, "127.0.0.1");
        String portStr = prefs.getString(SetupActivity.KEY_PORT, "4242");
        String authB64 = prefs.getString(SetupActivity.KEY_AUTH_SK, null);
        String encB64  = prefs.getString(SetupActivity.KEY_ENC_SK, null);
        if (authB64 == null || encB64 == null) { finish(); return; }

        byte[] authSk = Crypto.decodeBase64(authB64);
        byte[] encSk  = Crypto.decodeBase64(encB64);
        int    port   = Integer.parseInt(portStr);

        setStatus("Connecting...");
        executor.execute(() -> {
            try {
                conn = new DiaryConnection(authSk, encSk);
                conn.connect(host, port);
                handler.post(() -> setStatus(""));
            } catch (Exception e) {
                handler.post(() -> setStatus("Connection failed"));
            }
        });
    }

    private void save() {
        if (conn == null || saving) return;
        String text = editText.getText().toString();
        if (text.isEmpty()) return;

        saving = true;
        setStatus("Saving...");
        executor.execute(() -> {
            try {
                if (entryId > 0) conn.updateEntry(entryId, text);
                else             entryId = conn.postEntry(text);
                handler.post(() -> {
                    saving = false;
                    setResult(RESULT_OK);
                    setStatus("Saved");
                });
            } catch (Exception e) {
                handler.post(() -> {
                    saving = false;
                    setStatus("Save failed");
                });
            }
        });
    }

    private void setStatus(String msg) {
        getActionBar().setSubtitle(msg);
    }
}
