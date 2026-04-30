package uk.coko.forge.diary;

public class DiaryEntry {
    public final int    id;
    public final long   timestamp;
    public final String text;

    public DiaryEntry(int id, long timestamp, String text) {
        this.id        = id;
        this.timestamp = timestamp;
        this.text      = text;
    }

    public String preview() {
        if (text == null || text.isEmpty()) return "";
        int nl = text.indexOf('\n');
        String first = (nl >= 0) ? text.substring(0, nl) : text;
        return first.length() > 60 ? first.substring(0, 60) : first;
    }
}
