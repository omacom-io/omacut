.pragma library

// mm:ss.cc, shared by Main.qml and TrimBar.qml. Rounds to centiseconds first so
// boundary values render correctly (e.g. 59.999 -> "01:00.00", not "00:60.00").
function fmt(sec) {
    if (sec < 0 || isNaN(sec))
        sec = 0;
    var cs = Math.round(sec * 100);
    var m = Math.floor(cs / 6000);
    var s = (cs - m * 6000) / 100;
    return (m < 10 ? "0" : "") + m + ":" + (s < 10 ? "0" : "") + s.toFixed(2);
}
