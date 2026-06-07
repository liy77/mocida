//! Tiny syntax highlighter for the code editor. Produces colored byte spans for
//! the supported languages (`mui`, `crs`/Copper, `rs`/Rust). It's a lexer-level
//! tokenizer — comments, strings, numbers, keywords and Type-case identifiers —
//! not a full parser, which is plenty for an editor theme. Returned as
//! `(start, end, Color)` spans consumed by `UITextArea`'s highlighter callback.

use mocida::Color;

/// One dark theme (VS Code "Dark+"-ish). Uncolored text keeps the area's
/// default `textColor`, so only these token classes get a span.
const COMMENT: Color = Color::rgb(0x6A, 0x99, 0x55); // green
const STRING: Color = Color::rgb(0xCE, 0x91, 0x78); // soft orange
const NUMBER: Color = Color::rgb(0xB5, 0xCE, 0xA8); // light green
const KEYWORD: Color = Color::rgb(0x56, 0x9C, 0xD6); // blue
const TYPE: Color = Color::rgb(0x4E, 0xC9, 0xB0); // teal
const ATTR: Color = Color::rgb(0xC5, 0x86, 0xC0); // purple (annotations / props)

/// Languages we highlight. Anything else → no highlighting.
pub fn is_supported(lang: &str) -> bool {
    matches!(
        lang,
        "rs" | "crs" | "crm" | "mui" | "json"
            | "toml" | "kson" | "sh" | "bash" | "zsh" | "ps1" | "md" | "markdown"
    )
}

fn keywords(lang: &str) -> &'static [&'static str] {
    match lang {
        "rs" => &[
            // keywords (uppercase std types like Vec/Option are coloured as
            // Type by the Type-case rule, so they're intentionally not listed)
            "fn", "let", "mut", "const", "static", "async", "await", "unsafe", "extern",
            "struct", "enum", "union", "impl", "trait", "type", "pub", "use", "mod", "crate",
            "super", "self", "if", "else", "match", "for", "while", "loop", "in", "continue",
            "break", "return", "where", "as", "ref", "move", "dyn", "box", "macro", "yield", "try",
            // primitive types (lowercase → need to be listed to get a colour)
            "bool", "char", "str", "i8", "i16", "i32", "i64", "i128", "isize",
            "u8", "u16", "u32", "u64", "u128", "usize", "f32", "f64",
            "true", "false",
        ],
        "crs" | "crm" => &[
            "func", "let", "mut", "struct", "enum", "trait", "impl", "import", "from", "if", "else",
            "match", "for", "while", "loop", "return", "break", "continue", "as", "in", "pub",
            "const", "unsafe", "self", "true", "false", "vec",
        ],
        "mui" => &[
            "view", "App", "signal", "mut", "if", "else", "for", "in", "import", "from", "true",
            "false", "Window", "Screen",
        ],
        // JSON (incl. onda.project) + KSON (copper config): strings/numbers come
        // from the shared lexer; only the literals need a keyword color.
        "json" | "kson" => &["true", "false", "null"],
        // TOML (incl. Cargo.toml / Cargo.lock): # comments + strings/numbers from
        // the lexer; only the booleans (+ inf/nan) get a keyword color.
        "toml" => &["true", "false", "inf", "nan"],
        // POSIX shell.
        "sh" | "bash" | "zsh" => &[
            "if", "then", "else", "elif", "fi", "for", "while", "until", "do", "done",
            "case", "esac", "in", "function", "return", "local", "export", "readonly",
            "declare", "echo", "cd", "source", "exit", "break", "continue", "set", "unset",
            "alias", "shift", "trap", "eval", "exec", "test", "then", "select",
        ],
        // PowerShell (hyphenated cmdlets like Write-Host don't tokenise as one
        // identifier, so only the bare-word control keywords are listed).
        "ps1" => &[
            "if", "else", "elseif", "function", "param", "foreach", "for", "while", "do",
            "switch", "try", "catch", "finally", "throw", "break", "continue", "return",
            "begin", "process", "end", "filter", "in", "trap", "until", "class", "enum",
            "using", "Write", "Get", "Set", "New", "Remove",
        ],
        // Markdown has no keywords — headers/code are handled structurally below.
        "md" | "markdown" => &[],
        _ => &[],
    }
}

/// Highlight `text` for `lang` → byte-range color spans (sorted, non-overlapping).
pub fn spans(text: &str, lang: &str) -> Vec<(usize, usize, Color)> {
    let kw = keywords(lang);
    let b = text.as_bytes();
    let n = b.len();
    let mut out: Vec<(usize, usize, Color)> = Vec::new();
    let mut i = 0;

    let is_ident = |c: u8| c == b'_' || c.is_ascii_alphanumeric();

    // Per-language lexer flavours.
    let is_md = matches!(lang, "md" | "markdown");
    let hash_comment = matches!(lang, "toml" | "kson" | "sh" | "bash" | "zsh" | "ps1");
    let shell_var = matches!(lang, "sh" | "bash" | "zsh" | "ps1");

    while i < n {
        let c = b[i];

        // Markdown is prose, not code: handle its few token classes here and
        // ALWAYS continue, so the code-lexer rules below never mis-colour text
        // (numbers, quotes, etc.). ATX headers (line-start '#') → heading colour;
        // `code`/```fenced``` → string; **bold** → attr. Spans are bounded so an
        // unterminated marker can't recolour the rest of the file.
        if is_md {
            let at_line_start = i == 0 || b[i - 1] == b'\n';
            if at_line_start && c == b'#' {
                let s = i;
                while i < n && b[i] != b'\n' {
                    i += 1;
                }
                out.push((s, i, KEYWORD));
                continue;
            }
            if c == b'`' {
                let s = i;
                let mut ticks = 0;
                while i < n && b[i] == b'`' {
                    ticks += 1;
                    i += 1;
                }
                if ticks >= 3 {
                    loop {
                        if i >= n {
                            break;
                        }
                        if b[i] == b'`' {
                            let mut cnt = 0;
                            while i < n && b[i] == b'`' {
                                cnt += 1;
                                i += 1;
                            }
                            if cnt >= 3 {
                                break;
                            }
                        } else {
                            i += 1;
                        }
                    }
                } else {
                    while i < n && b[i] != b'`' && b[i] != b'\n' {
                        i += 1;
                    }
                    if i < n && b[i] == b'`' {
                        i += 1;
                    }
                }
                out.push((s, i.min(n), STRING));
                continue;
            }
            if c == b'*' && i + 1 < n && b[i + 1] == b'*' {
                let s = i;
                i += 2;
                while i + 1 < n && !(b[i] == b'*' && b[i + 1] == b'*') && b[i] != b'\n' {
                    i += 1;
                }
                if i + 1 < n && b[i] == b'*' && b[i + 1] == b'*' {
                    i += 2;
                }
                out.push((s, i.min(n), ATTR));
                continue;
            }
            i += 1;
            continue;
        }

        // Hash line comment (TOML / shell / PowerShell / KSON) — before the hex
        // and Rust-attr '#' rules so `# foo` reads as a comment, not a colour.
        if hash_comment && c == b'#' {
            let s = i;
            i += 1;
            while i < n && b[i] != b'\n' {
                i += 1;
            }
            out.push((s, i, COMMENT));
            continue;
        }
        // Shell / PowerShell variable: $name or ${name} (bounded to the line).
        if shell_var && c == b'$' {
            let s = i;
            i += 1;
            if i < n && b[i] == b'{' {
                while i < n && b[i] != b'}' && b[i] != b'\n' {
                    i += 1;
                }
                if i < n && b[i] == b'}' {
                    i += 1;
                }
            } else {
                while i < n && is_ident(b[i]) {
                    i += 1;
                }
            }
            out.push((s, i, KEYWORD));
            continue;
        }

        // Line comment // ... (and MUI/Copper use the same).
        if c == b'/' && i + 1 < n && b[i + 1] == b'/' {
            let s = i;
            i += 2;
            while i < n && b[i] != b'\n' {
                i += 1;
            }
            out.push((s, i, COMMENT));
            continue;
        }
        // Block comment /* ... */
        if c == b'/' && i + 1 < n && b[i + 1] == b'*' {
            let s = i;
            i += 2;
            while i + 1 < n && !(b[i] == b'*' && b[i + 1] == b'/') {
                i += 1;
            }
            i = (i + 2).min(n);
            out.push((s, i, COMMENT));
            continue;
        }
        // String literal "...". Bounded to the line: an unterminated quote (while
        // typing, or a genuine syntax error) must NOT run to EOF — that would
        // recolor everything below as a string, so the whole file's highlighting
        // appears to "break". Real multi-line Rust strings are rare; stopping at
        // the newline is the robust editor choice (a stray quote only mis-colors
        // its own line). The escape skip also can't jump over a newline.
        if c == b'"' {
            let s = i;
            i += 1;
            while i < n && b[i] != b'"' && b[i] != b'\n' {
                if b[i] == b'\\' && i + 1 < n && b[i + 1] != b'\n' {
                    i += 2;
                } else {
                    i += 1;
                }
            }
            if i < n && b[i] == b'"' {
                i += 1; // consume the closing quote (but never the newline)
            }
            out.push((s, i, STRING));
            continue;
        }
        // Char literal vs Rust lifetime. `'a` / `'static` are lifetimes (no
        // closing quote); `'c'` / `'\n'` are chars. Without this split a lifetime
        // runs the "string" scan to the next quote and mis-colours a whole region.
        if c == b'\'' {
            let s = i;
            let lifetime = lang == "rs"
                && i + 1 < n
                && (b[i + 1] == b'_' || b[i + 1].is_ascii_alphabetic())
                && !(i + 2 < n && b[i + 2] == b'\''); // 'a' is a char; 'a is a lifetime
            if lifetime {
                i += 1;
                while i < n && is_ident(b[i]) {
                    i += 1;
                }
                out.push((s, i, KEYWORD));
                continue;
            }
            // Char literal — also bounded to the line. A lone `'` (e.g. an
            // apostrophe typed mid-edit, or `let x = 'a` before the close) must
            // not swallow the rest of the file's colouring.
            i += 1;
            while i < n && b[i] != b'\'' && b[i] != b'\n' {
                if b[i] == b'\\' && i + 1 < n && b[i + 1] != b'\n' {
                    i += 2;
                } else {
                    i += 1;
                }
            }
            if i < n && b[i] == b'\'' {
                i += 1; // consume the closing quote (but never the newline)
            }
            out.push((s, i, STRING));
            continue;
        }
        // Rust attribute: #[ ... ] or #![ ... ] (balanced brackets).
        if lang == "rs"
            && c == b'#'
            && i + 1 < n
            && (b[i + 1] == b'[' || (b[i + 1] == b'!' && i + 2 < n && b[i + 2] == b'['))
        {
            let s = i;
            i += 1;
            if i < n && b[i] == b'!' {
                i += 1;
            }
            let mut depth = 0i32;
            while i < n {
                if b[i] == b'[' {
                    depth += 1;
                } else if b[i] == b']' {
                    depth -= 1;
                    i += 1;
                    if depth == 0 {
                        break;
                    }
                    continue;
                }
                i += 1;
            }
            out.push((s, i, ATTR));
            continue;
        }
        // Hex color literal (#rgb / #rrggbb) — common in MUI.
        if c == b'#' && i + 1 < n && (b[i + 1] as char).is_ascii_hexdigit() {
            let s = i;
            i += 1;
            while i < n && (b[i] as char).is_ascii_hexdigit() {
                i += 1;
            }
            out.push((s, i, NUMBER));
            continue;
        }
        // Number.
        if c.is_ascii_digit() {
            let s = i;
            while i < n && (b[i].is_ascii_digit() || b[i] == b'.' || b[i] == b'_') {
                i += 1;
            }
            out.push((s, i, NUMBER));
            continue;
        }
        // Rust attribute / Copper annotation: #[...] or @name
        if c == b'@' {
            let s = i;
            i += 1;
            while i < n && is_ident(b[i]) {
                i += 1;
            }
            out.push((s, i, ATTR));
            continue;
        }
        // Rust raw string: r"..." / r#"..."# (any number of #).
        if lang == "rs" && c == b'r' && i + 1 < n && (b[i + 1] == b'"' || b[i + 1] == b'#') {
            let s = i;
            let mut j = i + 1;
            let mut hashes = 0usize;
            while j < n && b[j] == b'#' {
                hashes += 1;
                j += 1;
            }
            if j < n && b[j] == b'"' {
                j += 1; // past the opening quote
                loop {
                    if j >= n {
                        break;
                    }
                    if b[j] == b'"' {
                        let mut k = j + 1;
                        let mut cnt = 0;
                        while k < n && cnt < hashes && b[k] == b'#' {
                            cnt += 1;
                            k += 1;
                        }
                        if cnt == hashes {
                            j = k;
                            break;
                        }
                    }
                    j += 1;
                }
                out.push((s, j.min(n), STRING));
                i = j.min(n);
                continue;
            }
            // not a raw string ('r' is a normal identifier) — fall through
        }
        // Identifier / keyword / Type. A trailing `!` marks a macro call.
        if c == b'_' || c.is_ascii_alphabetic() {
            let s = i;
            while i < n && is_ident(b[i]) {
                i += 1;
            }
            let word = &text[s..i];
            if kw.contains(&word) {
                out.push((s, i, KEYWORD));
            } else if lang == "rs" && i < n && b[i] == b'!' {
                // macro invocation: `println!`, `vec!`, … — colour name + bang
                i += 1;
                out.push((s, i, ATTR));
            } else if word.as_bytes()[0].is_ascii_uppercase() {
                out.push((s, i, TYPE));
            }
            continue;
        }
        i += 1;
    }
    out
}
