package uk.coko.forge.diary;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.SharedPreferences;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.widget.ArrayAdapter;
import android.widget.ListView;
import android.widget.TextView;
import android.widget.Toast;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public class MainActivity extends Activity {

    static final String  EXTRA_ENTRY_ID   = "entry_id";
    static final String  EXTRA_ENTRY_TEXT = "entry_text";
    static final String  EXTRA_ENTRY_TS   = "entry_ts";
    static final int     REQ_EDITOR       = 10;

    private final ExecutorService executor = Executors.newSingleThreadExecutor();
    private final Handler         handler  = new Handler(Looper.getMainLooper());

    private DiaryConnection  conn;
    private List<DiaryEntry> entries = new ArrayList<>();
    private ArrayAdapter<String> adapter;
    private ListView listView;

    private final SimpleDateFormat sdf = new SimpleDateFormat("yyyy-MM-dd HH:mm", Locale.getDefault());

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        listView = findViewById(R.id.list_entries);
        adapter  = new ArrayAdapter<>(this, android.R.layout.simple_list_item_1, new ArrayList<>());
        listView.setAdapter(adapter);

        getActionBar().setTitle("Diary");

        listView.setOnItemClickListener((parent, view, pos, id) -> openViewer(pos));
        listView.setOnItemLongClickListener((parent, view, pos, id) -> {
            showEntryMenu(pos);
            return true;
        });

        findViewById(R.id.btn_new).setOnClickListener(v -> openEditor(-1, null, System.currentTimeMillis() / 1000));
        findViewById(R.id.btn_settings).setOnClickListener(v -> openSetup());
        findViewById(R.id.btn_reload).setOnClickListener(v -> loadEntries());

        Crypto.sodiumInit();
        connectAndLoad();
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        executor.execute(() -> { if (conn != null) conn.disconnect(); });
        executor.shutdown();
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode == REQ_EDITOR) loadEntries();
    }

    private void connectAndLoad() {
        SharedPreferences prefs = getSharedPreferences(SetupActivity.PREFS, MODE_PRIVATE);
        String host    = prefs.getString(SetupActivity.KEY_HOST, null);
        String portStr = prefs.getString(SetupActivity.KEY_PORT, "4242");
        String authB64 = prefs.getString(SetupActivity.KEY_AUTH_SK, null);
        String encB64  = prefs.getString(SetupActivity.KEY_ENC_SK, null);

        if (host == null || authB64 == null || encB64 == null) {
            openSetup(); return;
        }

        byte[] authSk = Crypto.decodeBase64(authB64);
        byte[] encSk  = Crypto.decodeBase64(encB64);
        int    port   = Integer.parseInt(portStr);

        setStatus("Connecting...");
        executor.execute(() -> {
            try {
                conn = new DiaryConnection(authSk, encSk);
                conn.connect(host, port);
                handler.post(() -> { setStatus(""); loadEntries(); });
            } catch (Exception e) {
                handler.post(() -> setStatus("Connection failed: " + e.getMessage()));
            }
        });
    }

    private void loadEntries() {
        if (conn == null) return;
        setStatus("Loading...");
        executor.execute(() -> {
            try {
                List<DiaryEntry> list = conn.getEntries();
                list.sort((a, b) -> Long.compare(b.timestamp, a.timestamp));
                handler.post(() -> {
                    entries = list;
                    refreshList();
                    setStatus("");
                });
            } catch (Exception e) {
                handler.post(() -> setStatus("Error: " + e.getMessage()));
            }
        });
    }

    private void refreshList() {
        adapter.clear();
        for (DiaryEntry e : entries) {
            String date = sdf.format(new Date(e.timestamp * 1000));
            adapter.add(date + "  " + e.preview());
        }
    }

    private void showEntryMenu(int pos) {
        DiaryEntry e = entries.get(pos);
        new AlertDialog.Builder(this)
            .setItems(new String[]{"Edit", "Delete"}, (dialog, which) -> {
                if (which == 0) openEditor(e.id, e.text, e.timestamp);
                else            confirmDelete(pos);
            }).show();
    }

    private void confirmDelete(int pos) {
        new AlertDialog.Builder(this)
            .setMessage("Delete this entry?")
            .setPositiveButton("Delete", (d, w) -> deleteEntry(pos))
            .setNegativeButton("Cancel", null)
            .show();
    }

    private void deleteEntry(int pos) {
        DiaryEntry e = entries.get(pos);
        executor.execute(() -> {
            try {
                conn.deleteEntry(e.id);
                handler.post(() -> { entries.remove(pos); refreshList(); });
            } catch (Exception ex) {
                handler.post(() -> Toast.makeText(this, "Delete failed", Toast.LENGTH_SHORT).show());
            }
        });
    }

    private void openViewer(int pos) {
        DiaryEntry e = entries.get(pos);
        openEditor(e.id, e.text, e.timestamp);
    }

    private void openEditor(int id, String text, long ts) {
        Intent intent = new Intent(this, EntryActivity.class);
        intent.putExtra(EXTRA_ENTRY_ID,   id);
        intent.putExtra(EXTRA_ENTRY_TEXT, text != null ? text : "");
        intent.putExtra(EXTRA_ENTRY_TS,   ts);
        startActivityForResult(intent, REQ_EDITOR);
    }

    private void openSetup() {
        startActivity(new Intent(this, SetupActivity.class));
    }

    private void setStatus(String msg) {
        TextView tv = findViewById(R.id.text_status);
        tv.setText(msg);
        tv.setVisibility(msg.isEmpty() ? android.view.View.GONE : android.view.View.VISIBLE);
    }
}
