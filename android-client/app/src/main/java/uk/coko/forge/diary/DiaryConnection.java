package uk.coko.forge.diary;

import android.util.Base64;
import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.PrintWriter;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

public class DiaryConnection {

    private Socket         socket;
    private BufferedReader in;
    private PrintWriter    out;

    private final byte[] authSk;
    private final byte[] authPk;
    private final byte[] encPk;
    private final byte[] encSk;

    public DiaryConnection(byte[] authSk, byte[] encSk) {
        this.authSk = authSk;
        this.authPk = Crypto.authPkFromSk(authSk);
        this.encSk  = encSk;
        this.encPk  = Crypto.encPkFromSk(encSk);
    }

    public void connect(String host, int port) throws IOException {
        socket = new Socket(host, port);
        in  = new BufferedReader(new InputStreamReader(socket.getInputStream(), StandardCharsets.UTF_8));
        out = new PrintWriter(socket.getOutputStream(), true);

        sendLine("HELLO");
        String line = readLine();
        if (!line.startsWith("CHALLENGE ")) throw new IOException("Expected CHALLENGE");

        byte[] challenge = Base64.decode(line.substring(10), Base64.DEFAULT);
        byte[] sig       = Crypto.signChallenge(challenge, authSk);

        String pkB64  = Crypto.encodeBase64(authPk);
        String sigB64 = Crypto.encodeBase64(sig);
        sendLine("AUTH " + pkB64 + " " + sigB64);

        String resp = readLine();
        if (resp.equals("REGISTER")) {
            String epkB64 = Crypto.encodeBase64(encPk);
            sendLine("REGISTER " + epkB64);
            resp = readLine();
            if (!resp.equals("OK")) throw new IOException("REGISTER failed: " + resp);
        } else if (!resp.equals("OK")) {
            throw new IOException("Auth failed: " + resp);
        }
    }

    public void disconnect() {
        try {
            if (out != null) { sendLine("QUIT"); readLine(); }
            if (socket != null) socket.close();
        } catch (IOException ignored) {}
    }

    public int postEntry(String text) throws IOException {
        String b64 = Crypto.sealToBase64(text, encPk);
        if (b64 == null) throw new IOException("Encryption failed");
        sendLine("POST " + b64);
        String resp = readLine();
        if (!resp.startsWith("OK ")) throw new IOException("POST failed: " + resp);
        return Integer.parseInt(resp.substring(3).trim());
    }

    public int updateEntry(int id, String text) throws IOException {
        String b64 = Crypto.sealToBase64(text, encPk);
        if (b64 == null) throw new IOException("Encryption failed");
        sendLine("UPDATE " + id + " " + b64);
        String resp = readLine();
        if (!resp.equals("OK")) throw new IOException("UPDATE failed: " + resp);
        return id;
    }

    public void deleteEntry(int id) throws IOException {
        sendLine("DELETE " + id);
        String resp = readLine();
        if (!resp.equals("OK")) throw new IOException("DELETE failed: " + resp);
    }

    public List<DiaryEntry> getEntries() throws IOException {
        sendLine("GET");
        String header = readLine();
        if (!header.startsWith("ENTRIES ")) throw new IOException("Expected ENTRIES");
        int count = Integer.parseInt(header.substring(8).trim());

        List<DiaryEntry> entries = new ArrayList<>(count);
        for (int i = 0; i < count; i++) {
            String line = readLine();
            int s1 = line.indexOf(' ');
            int s2 = line.indexOf(' ', s1 + 1);
            if (s1 < 0 || s2 < 0) continue;

            int    id   = Integer.parseInt(line.substring(0, s1));
            long   ts   = Long.parseLong(line.substring(s1 + 1, s2));
            String b64  = line.substring(s2 + 1);
            String text = Crypto.unsealFromBase64(b64, encPk, encSk);
            entries.add(new DiaryEntry(id, ts, text != null ? text : "[could not decrypt]"));
        }
        return entries;
    }

    private void sendLine(String line) {
        out.println(line);
    }

    private String readLine() throws IOException {
        String line = in.readLine();
        if (line == null) throw new IOException("Connection closed");
        return line;
    }
}
