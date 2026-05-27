# ttedi
a lightweight terminal text editor for Windows

<img src="image/README/ttedi.png" alt="ttedi" width="25%">

Usage: `ttedi <filename>`

## Supported Languages (for syntax highlight):
- Java
- C/C++
- Python
- Javascript

## Configuration

ttedi supports a config file at `%USERPROFILE%\.ttedirc`.

```ini
# Global settings
tab_stop=4
line_numbers=1
auto_close_brackets=1
quit_times=1
status_timeout=5

# Language-specific overrides
[c]
tab_stop=4

[python]
tab_stop=4

[js]
tab_stop=2

[java]
tab_stop=4
```

| Setting | Description | Default |
|---------|-------------|---------|
| `tab_stop` | Indent width (spaces) | 4 |
| `line_numbers` | Show line number gutter | 1 |
| `auto_close_brackets` | Auto-insert closing `}`, `]`, `)` | 1 |
| `quit_times` | Ctrl+Q presses to force-quit with unsaved changes | 1 |
| `status_timeout` | Seconds before status message disappears | 5 |
| `color_default` | Default text color | 37 (white) |
| `color_comment` | Single-line comment color | 92 (bright green) |
| `color_mlcomment` | Multi-line comment color | 92 (bright green) |
| `color_keyword1` | Keyword color | 35 (magenta) |
| `color_keyword2` | Type/literal color | 36 (cyan) |
| `color_string` | String literal color | 33 (yellow) |
| `color_number` | Number literal color | 32 (green) |
| `color_match` | Search match highlight | 100 (gray bg) |
| `color_function` | Function name color | 93 (bright yellow) |

Language sections (`[c]`, `[python]`, `[js]`, `[java]`) override `tab_stop` per filetype.

## Credits:

extended from [snaptoken's tutorial](https://viewsourcecode.org/snaptoken/kilo/).

inspired by [antirez's kilo](https://antirez.com/news/108) text editor.

## AI Usage:

built with assistance from [Amazon Q Developer](https://aws.amazon.com/q/developer/).
