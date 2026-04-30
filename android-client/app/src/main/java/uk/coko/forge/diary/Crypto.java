package uk.coko.forge.diary;

import android.util.Base64;
import java.nio.charset.StandardCharsets;

public class Crypto {

    static { System.loadLibrary("diary_crypto"); }

    public static native int     sodiumInit();
    public static native byte[]  authPkFromSk(byte[] authSk);
    public static native byte[]  encPkFromSk(byte[] encSk);
    public static native byte[]  signChallenge(byte[] challenge, byte[] authSk);
    public static native byte[]  seal(byte[] msg, byte[] encPk);
    public static native byte[]  unseal(byte[] cipher, byte[] encPk, byte[] encSk);

    public static byte[] decodeBase64(String b64) {
        return Base64.decode(b64, Base64.DEFAULT);
    }

    public static String encodeBase64(byte[] data) {
        return Base64.encodeToString(data, Base64.NO_WRAP);
    }

    public static String sealToBase64(String plaintext, byte[] encPk) {
        byte[] msg    = plaintext.getBytes(StandardCharsets.UTF_8);
        byte[] cipher = seal(msg, encPk);
        if (cipher == null) return null;
        return encodeBase64(cipher);
    }

    public static String unsealFromBase64(String b64, byte[] encPk, byte[] encSk) {
        byte[] cipher = decodeBase64(b64);
        byte[] plain  = unseal(cipher, encPk, encSk);
        if (plain == null) return null;
        return new String(plain, StandardCharsets.UTF_8);
    }

    public static String toHex(byte[] data) {
        StringBuilder sb = new StringBuilder(data.length * 2);
        for (byte b : data) sb.append(String.format("%02x", b));
        return sb.toString();
    }
}
