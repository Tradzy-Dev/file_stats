# File Stats – Text File Analysis

A small C++ utility that computes statistics on a text file.

---

## Features
- Counts lines, words, and characters (bytes)  
- Analyzes word frequency  
- Exports results to JSON  

---

## Compilation
```bash
g++ file_stats.cpp -o file_stats -std=c++17
```

---

## Usage Examples

### Basic statistics
```bash
./file_stats README.md
```

**Output:**
```
File:   README.md
Lines:  42
Words:  312
Bytes:  2345
Top 20 words (case-insensitive):
       15  file
       12  stats
       10  text
       ...
```

---

### Show top 50 words
```bash
./file_stats book.txt --top 50
```

---

### Export results to JSON
```bash
./file_stats data.txt --json results.json
```

**Creates `results.json`:**
```json
{
  "tool": "file-stats",
  "timestamp": "2025-09-22T18:00:00Z",
  "input_path": "data.txt",
  "lines": 120,
  "words": 845,
  "bytes": 6204,
  "case_sensitive": false,
  "top_words": [
    { "word": "data", "count": 34 },
    { "word": "analysis", "count": 18 }
  ]
}
```

---

### Case-sensitive word frequency
```bash
./file_stats log.txt --case-sensitive
```

---

## License
MIT (free to use, modify, and distribute)
