# Markdown to the plain text a release ships to DOS.
#
# EDIT.COM reads 78 columns comfortably, which is where the manifest is kept and
# where this lands too. Markdown that a browser renders reads badly there: a
# pipe table is unreadable without the renderer, backticks are noise, and a link
# hides the address that a reader with no browser needs.
#
# Deliberately not a general converter. It handles what this project's README
# uses and nothing else, because the alternative is carrying a second README by
# hand and letting the two drift.
#
#     awk -f txtify.awk README.md > README.TXT
#
# Line endings are left to the caller; a release converts them to CRLF.

# A paragraph rewraps to WIDTH. pfx1 and pfx2 carry the first-line and
# continuation indents, which is how a bullet keeps its hanging indent when it
# runs past one line.
function flush_para(   n, line, word, i, words, pre) {
    if (para == "") return
    n = split(para, words, " ")
    line = ""; pre = pfx1
    for (i = 1; i <= n; i++) {
        word = words[i]
        if (line == "") { line = pre word }
        else if (length(line) + 1 + length(word) <= WIDTH) { line = line " " word }
        else { print line; pre = pfx2; line = pre word }
    }
    if (line != "") print line
    para = ""; pfx1 = ""; pfx2 = ""
}

# Drop the markup a plain reader does not need, and keep what carries meaning:
# a link's address survives, because the reader may have no way to look it up.
function clean(s) {
    gsub(/\*\*/, "", s)
    gsub(/`/, "", s)
    while (match(s, /\[[^]]*\]\([^)]*\)/)) {
        t = substr(s, RSTART, RLENGTH)
        lb = index(t, "]")
        txt = substr(t, 2, lb - 2)
        url = substr(t, lb + 2, length(t) - lb - 2)
        s = substr(s, 1, RSTART - 1) \
            (url ~ /^https?:/ ? txt " <" url ">" : txt) \
            substr(s, RSTART + RLENGTH)
    }
    return s
}

# A table row becomes fixed columns. Two passes are not worth it for tables this
# small, so the widths come from the header and the separator row is dropped.
function emit_row(s,   n, c, i, out) {
    sub(/^[ \t]*\|/, "", s); sub(/\|[ \t]*$/, "", s)
    n = split(s, c, "|")
    out = ""
    for (i = 1; i <= n; i++) {
        gsub(/^[ \t]+|[ \t]+$/, "", c[i])
        out = out sprintf("%-*s", (i == n ? 0 : colw[i] + 2), clean(c[i]))
    }
    sub(/[ \t]+$/, "", out)
    print out
}

BEGIN { WIDTH = 78; para = ""; pfx1 = ""; pfx2 = ""; intable = 0 }

/^```/          { flush_para(); incode = !incode; next }
incode          { print; next }

# an indented block is already laid out; pass it through
/^    /         { flush_para(); print; next }

/^\|[ \t]*-+/   { next }                       # the separator row
/^\|/ {
    if (!intable) {
        flush_para(); intable = 1
        hdr = $0
        sub(/^[ \t]*\|/, "", hdr); sub(/\|[ \t]*$/, "", hdr)
        nc = split(hdr, hc, "|")
        for (i = 1; i <= nc; i++) {
            gsub(/^[ \t]+|[ \t]+$/, "", hc[i])
            colw[i] = length(clean(hc[i]))
        }
    }
    # widen a column to whatever the widest cell in it needs
    row = $0
    sub(/^[ \t]*\|/, "", row); sub(/\|[ \t]*$/, "", row)
    n = split(row, c, "|")
    for (i = 1; i <= n; i++) {
        gsub(/^[ \t]+|[ \t]+$/, "", c[i])
        if (length(clean(c[i])) > colw[i]) colw[i] = length(clean(c[i]))
    }
    rows[++nrows] = $0
    next
}
intable && !/^\|/ {
    for (i = 1; i <= nrows; i++) emit_row(rows[i])
    nrows = 0; intable = 0
}

/^#+ /          { flush_para()
                  h = $0; sub(/^#+ /, "", h)
                  print toupper(clean(h)); next }

/^[ \t]*$/      { flush_para(); print ""; next }

# A bullet opens a paragraph rather than printing one, so a line continuing it
# joins in and wraps under the text instead of under the dash.
/^[-*] /        { flush_para()
                  b = $0; sub(/^[-*] +/, "", b)
                  para = clean(b); pfx1 = "  - "; pfx2 = "    "; next }

                { para = (para == "" ? clean($0) : para " " clean($0)) }

END {
    if (intable) for (i = 1; i <= nrows; i++) emit_row(rows[i])
    flush_para()
}
