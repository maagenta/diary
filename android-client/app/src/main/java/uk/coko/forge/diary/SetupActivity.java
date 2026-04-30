package uk.coko.forge.diary;

import android.app.Activity;
import android.content.Intent;
import android.content.SharedPreferences;
import android.net.Uri;
import android.os.Bundle;
import android.view.View;
import android.widget.Button;
import android.widget.EditText;
import android.widget.TextView;
import android.widget.Toast;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;

public class SetupActivity extends Activity {

    private static final int REQ_AUTH_KEY = 1;
    private static final int REQ_ENC_KEY  = 2;

    static final String PREFS       = "diary_prefs";
    static final String KEY_HOST    = "host";
    static final String KEY_PORT    = "port";
    static final String KEY_AUTH_SK = "auth_sk_b64";
    static final String KEY_ENC_SK  = "enc_sk_b64";

    private EditText hostEdit, portEdit;
    private TextView authKeyLabel, encKeyLabel;
    private byte[]   authSkBytes, encSkBytes;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_setup);

        hostEdit     = findViewById(R.id.edit_host);
        portEdit     = findViewById(R.id.edit_port);
        authKeyLabel = findViewById(R.id.label_auth_key);
        encKeyLabel  = findViewById(R.id.label_enc_key);

        SharedPreferences prefs = getSharedPreferences(PREFS, MODE_PRIVATE);
        hostEdit.setText(prefs.getString(KEY_HOST, ""));
        portEdit.setText(prefs.getString(KEY_PORT, "4242"));

        Button btnAuthKey = findViewById(R.id.btn_auth_key);
        Button btnEncKey  = findViewById(R.id.btn_enc_key);
        Button btnSave    = findViewById(R.id.btn_save);

        btnAuthKey.setOnClickListener(v -> pickFile(REQ_AUTH_KEY));
        btnEncKey.setOnClickListener(v  -> pickFile(REQ_ENC_KEY));
        btnSave.setOnClickListener(v    -> save());
    }

    private void pickFile(int requestCode) {
        Intent intent = new Intent(Intent.ACTION_GET_CONTENT);
        intent.setType("*/*");
        startActivityForResult(intent, requestCode);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (resultCode != RESULT_OK || data == null) return;
        Uri uri = data.getData();
        try {
            byte[] bytes = readUri(uri);
            String b64line = new String(bytes).trim();
            byte[] keyBytes = Crypto.decodeBase64(b64line);
            if (requestCode == REQ_AUTH_KEY) {
                authSkBytes = keyBytes;
                authKeyLabel.setText("auth.key: loaded (" + keyBytes.length + " bytes)");
            } else {
                encSkBytes = keyBytes;
                encKeyLabel.setText("enc.key: loaded (" + keyBytes.length + " bytes)");
            }
        } catch (IOException e) {
            Toast.makeText(this, "Error reading file", Toast.LENGTH_SHORT).show();
        }
    }

    private void save() {
        String host = hostEdit.getText().toString().trim();
        String port = portEdit.getText().toString().trim();

        if (host.isEmpty() || port.isEmpty() || authSkBytes == null || encSkBytes == null) {
            Toast.makeText(this, "Fill in all fields and load both key files", Toast.LENGTH_SHORT).show();
            return;
        }

        SharedPreferences.Editor ed = getSharedPreferences(PREFS, MODE_PRIVATE).edit();
        ed.putString(KEY_HOST, host);
        ed.putString(KEY_PORT, port);
        ed.putString(KEY_AUTH_SK, Crypto.encodeBase64(authSkBytes));
        ed.putString(KEY_ENC_SK,  Crypto.encodeBase64(encSkBytes));
        ed.apply();

        startActivity(new Intent(this, MainActivity.class));
        finish();
    }

    private byte[] readUri(Uri uri) throws IOException {
        InputStream is = getContentResolver().openInputStream(uri);
        ByteArrayOutputStream buf = new ByteArrayOutputStream();
        byte[] tmp = new byte[4096];
        int n;
        while ((n = is.read(tmp)) != -1) buf.write(tmp, 0, n);
        is.close();
        return buf.toByteArray();
    }
}
