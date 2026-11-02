/**
 * @file    json.c
 * @brief   LPC JSON 编解码实现



 *
 * @par Description:
 * 为 LPC 提供 JSON 序列化与反序列化能力，兼容当前项目运行需求。
 */

#pragma strict_types

#define to_string(x) ("" + (x))

#define JSON_DECODE_PARSE_TEXT      0
#define JSON_DECODE_PARSE_POS       1
#define JSON_DECODE_PARSE_LINE      2
#define JSON_DECODE_PARSE_CHAR      3
#define JSON_DECODE_PARSE_DEPTH     4
#define JSON_DECODE_PARSE_FIELDS    5

#define MAX_JSON_NESTING_DEPTH      20

private mixed json_decode_parse_value(mixed* parse);
private varargs mixed json_decode_parse_string(mixed* parse, int initiator_checked);
private mixed json_decode_parse_number(mixed* parse);
private varargs void json_decode_parse_error(mixed* parse, string msg, int ch);
private string json_encode_value(mixed value, int depth);

private void json_decode_parse_next_char(mixed* parse) {
    parse[JSON_DECODE_PARSE_POS]++;
    parse[JSON_DECODE_PARSE_CHAR]++;
}

private void json_decode_parse_next_chars(mixed* parse, int num) {
    parse[JSON_DECODE_PARSE_POS] += num;
    parse[JSON_DECODE_PARSE_CHAR] += num;
}

private void json_decode_parse_next_line(mixed* parse) {
    parse[JSON_DECODE_PARSE_POS]++;
    parse[JSON_DECODE_PARSE_LINE]++;
    parse[JSON_DECODE_PARSE_CHAR] = 1;
}

private void json_decode_skip_whitespaces(mixed* parse) {
    int ch;
    int len;
    string text;

    text = parse[JSON_DECODE_PARSE_TEXT];
    if (!stringp(text)) {
        error("json_decode: Invalid input");
    }
    len = strlen(text);
    if (parse[JSON_DECODE_PARSE_POS] + 1 >= len) {
        parse[JSON_DECODE_PARSE_POS] = len;
        return;
    }
    while(1) {
      json_decode_parse_next_char(parse);
      if (parse[JSON_DECODE_PARSE_POS] >= len) {
          json_decode_parse_error(parse, "Unexpected end of data");
      }
      ch = parse[JSON_DECODE_PARSE_TEXT][parse[JSON_DECODE_PARSE_POS]];
      if (ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t') {
        continue;
      } else {
        return ;
      }
    }
}

private int json_decode_hexdigit(int ch) {
    switch(ch) {
    case '0':
        return 0;
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
        return ch - '0';
    case 'a':
    case 'A':
        return 10;
    case 'b':
    case 'B':
        return 11;
    case 'c':
    case 'C':
        return 12;
    case 'd':
    case 'D':
        return 13;
    case 'e':
    case 'E':
        return 14;
    case 'f':
    case 'F':
        return 15;
    }
    return -1;
}

private varargs int json_decode_parse_at_token(mixed* parse, string token, int start) {
    int i, j;
    for(i = start, j = strlen(token); i < j; i++)
        if(parse[JSON_DECODE_PARSE_TEXT][parse[JSON_DECODE_PARSE_POS] + i] != token[i])
            return 0;
    return 1;
}

private varargs void json_decode_parse_error(mixed* parse, string msg, int ch) {
    if(ch)
        msg = sprintf("%s, '%c'", msg, ch);
    msg = sprintf("%s @ line %d char %d\n", msg, parse[JSON_DECODE_PARSE_LINE], parse[JSON_DECODE_PARSE_CHAR]);
    error(msg);
}

private mixed json_decode_parse_object(mixed* parse) {
    mapping out = ([]);
    int done = 0;
    mixed key, value;
    int found_non_whitespace, found_sep, found_comma;

    // Check recursion depth
    parse[JSON_DECODE_PARSE_DEPTH]++;
    if(parse[JSON_DECODE_PARSE_DEPTH] > MAX_JSON_NESTING_DEPTH) {
        json_decode_parse_error(parse, "Maximum JSON nesting depth exceeded");
    }

    json_decode_parse_next_char(parse);
    if(parse[JSON_DECODE_PARSE_TEXT][parse[JSON_DECODE_PARSE_POS]] == '}') {
        done = 1;
        json_decode_parse_next_char(parse);
    }
    while(!done) {
        found_non_whitespace = 0;
        while(!found_non_whitespace) {
            switch(parse[JSON_DECODE_PARSE_TEXT][parse[JSON_DECODE_PARSE_POS]]) {
            case 0:
                json_decode_parse_error(parse, "Unexpected end of data");
            case ' ':
            case '\t':
            case '\r':
                json_decode_parse_next_char(parse);
                break;
            case 0x0c:
            case '\n':
                json_decode_parse_next_line(parse);
                break;
            default:
                found_non_whitespace = 1;
                break;
            }
        }
        key = json_decode_parse_string(parse);
        found_sep = 0;
        while(!found_sep) {
            int ch = parse[JSON_DECODE_PARSE_TEXT][parse[JSON_DECODE_PARSE_POS]];
            switch(ch) {
            case 0:
                json_decode_parse_error(parse, "Unexpected end of data");
            case ':':
                found_sep = 1;
                json_decode_parse_next_char(parse);
                break;
            case ' ':
            case '\t':
            case '\r':
                json_decode_parse_next_char(parse);
                break;
            case 0x0c:
            case '\n':
                json_decode_parse_next_line(parse);
                break;
            default:
                json_decode_parse_error(parse, "Unexpected character", ch);
            }
        }
        value = json_decode_parse_value(parse);
        found_comma = 0;
        while(!found_comma && !done) {
            int ch = parse[JSON_DECODE_PARSE_TEXT][parse[JSON_DECODE_PARSE_POS]];
            switch(ch) {
            case 0:
                json_decode_parse_error(parse, "Unexpected end of data");
            case ',':
                found_comma = 1;
                json_decode_parse_next_char(parse);
                break;
            case '}':
                done = 1;
                json_decode_parse_next_char(parse);
                break;
            case ' ':
            case '\t':
            case '\r':
                json_decode_parse_next_char(parse);
                break;
            case 0x0c:
            case '\n':
                json_decode_parse_next_line(parse);
                break;
            default:
                json_decode_parse_error(parse, "Unexpected character", ch);
            }
        }
        out[key] = value;
    }
    parse[JSON_DECODE_PARSE_DEPTH]--;
    return out;
}

private mixed json_decode_parse_array(mixed* parse) {
    mixed* out = ({});
    int done = 0;
    int found_comma;

    // Check recursion depth
    parse[JSON_DECODE_PARSE_DEPTH]++;
    if(parse[JSON_DECODE_PARSE_DEPTH] > MAX_JSON_NESTING_DEPTH) {
        json_decode_parse_error(parse, "Maximum JSON nesting depth exceeded");
    }

    json_decode_parse_next_char(parse);
    if(parse[JSON_DECODE_PARSE_TEXT][parse[JSON_DECODE_PARSE_POS]] == ']') {
        done = 1;
        json_decode_parse_next_char(parse);
    }
    while(!done) {
        mixed value = json_decode_parse_value(parse);
        found_comma = 0;
        while(!found_comma && !done) {
            int ch = parse[JSON_DECODE_PARSE_TEXT][parse[JSON_DECODE_PARSE_POS]];
            switch(ch) {
            case 0:
                json_decode_parse_error(parse, "Unexpected end of data");
            case ',':
                found_comma = 1;
                json_decode_parse_next_char(parse);
                break;
            case ']':
                done = 1;
                json_decode_parse_next_char(parse);
                break;
            case ' ':
            case '\t':
            case '\r':
                json_decode_parse_next_char(parse);
                break;
            case 0x0c:
            case '\n':
                json_decode_parse_next_line(parse);
                break;
            default:
                json_decode_parse_error(parse, "Unexpected character", ch);
            }
        }
        out += ({ value });
    }
    parse[JSON_DECODE_PARSE_DEPTH]--;
    return out;
}

/* 读取 \uXXXX 的 4 位十六进制；失败返回 -1。 */
private int json_decode_read_hex4(mixed* parse) {
    int i;
    int ch;
    int value;
    int digit;

    value = 0;
    for (i = 0; i < 4; i++) {
        json_decode_parse_next_char(parse);
        ch = parse[JSON_DECODE_PARSE_TEXT][parse[JSON_DECODE_PARSE_POS]];
        digit = json_decode_hexdigit(ch);
        if (digit < 0) {
            return -1;
        }
        value = value * 16 + digit;
    }
    return value;
}

/* 反斜杠转义：支持 JSON 允许的 8 种转义与 \uXXXX（含代理对）。
 * 未知转义按非法输入报错，不静默吞掉——静默会让 encode/decode 不对称。 */
private string json_decode_read_escape(mixed* parse) {
    int ch;
    int codepoint;
    int low;
    int high;

    json_decode_parse_next_char(parse);
    ch = parse[JSON_DECODE_PARSE_TEXT][parse[JSON_DECODE_PARSE_POS]];
    switch (ch) {
    case '"':  return "\"";
    case '\\': return "\\";
    case '/':  return "/";
    case 'b':  return "\b";
    case 'f':  return sprintf("%c", 0x0c);
    case 'n':  return "\n";
    case 'r':  return "\r";
    case 't':  return "\t";
    case 'u':
        high = json_decode_read_hex4(parse);
        if (high < 0)
            json_decode_parse_error(parse, "Invalid unicode escape", ch);

        // 代理对：高位 0xD800-0xDBFF 必须紧跟低位转义，否则按非法输入处理。
        if (high >= 0xD800 && high <= 0xDBFF) {
            json_decode_parse_next_char(parse);
            if (parse[JSON_DECODE_PARSE_TEXT][parse[JSON_DECODE_PARSE_POS]] != '\\')
                json_decode_parse_error(parse, "Unpaired unicode surrogate", ch);
            json_decode_parse_next_char(parse);
            if (parse[JSON_DECODE_PARSE_TEXT][parse[JSON_DECODE_PARSE_POS]] != 'u')
                json_decode_parse_error(parse, "Unpaired unicode surrogate", ch);
            low = json_decode_read_hex4(parse);
            if (low < 0xDC00 || low > 0xDFFF)
                json_decode_parse_error(parse, "Invalid unicode surrogate pair", ch);
            codepoint = 0x10000 + ((high - 0xD800) << 10) + (low - 0xDC00);
        } else if (high >= 0xDC00 && high <= 0xDFFF) {
            json_decode_parse_error(parse, "Unpaired unicode surrogate", ch);
        } else {
            codepoint = high;
        }
        return sprintf("%c", codepoint);
    case 0:
        json_decode_parse_error(parse, "Unexpected end of data in string");
    default:
        json_decode_parse_error(parse, "Invalid escape sequence", ch);
    }
    return "";
}

private varargs mixed json_decode_parse_string(mixed* parse, int initiator_checked) {
    int ch;
    string out;

    if (!initiator_checked) {
        ch = parse[JSON_DECODE_PARSE_TEXT][parse[JSON_DECODE_PARSE_POS]];
        if (!ch)
            json_decode_parse_error(parse, "Unexpected end of data");
        if (ch != '"')
            json_decode_parse_error(parse, "Unexpected character", ch);
    }

    json_decode_parse_next_char(parse);
    out = "";

    /* 逐字符扫描：只有未转义的 " 才结束字符串，转义序列就地还原。
     * 原实现只在反斜杠处置 esc_state 却从不使用它，导致 \" 提前结束、
     * 且返回的是原始转义文本，encode/decode 不往返。 */
    while (1) {
        ch = parse[JSON_DECODE_PARSE_TEXT][parse[JSON_DECODE_PARSE_POS]];
        switch (ch) {
        case 0:
            json_decode_parse_error(parse, "Unexpected end of data in string");
        case '"':
            json_decode_parse_next_char(parse);
            return out;
        case '\\':
            out += json_decode_read_escape(parse);
            break;
        default:
            // 未转义的控制字符按宽容读取（历史存档可能带原始控制字节），
            // 编码侧统一转义，新写入的数据始终是合法 JSON。
            out += sprintf("%c", ch);
            break;
        }
        json_decode_parse_next_char(parse);
    }

    return out;
}

private mixed json_decode_parse_number(mixed* parse) {
    int start;
    int ch;
    int has_dot;
    int has_exp;
    string text;
    int int_value;
    float float_value;

    start = parse[JSON_DECODE_PARSE_POS];
    ch = parse[JSON_DECODE_PARSE_TEXT][parse[JSON_DECODE_PARSE_POS]];
    if (ch == '-') {
        json_decode_parse_next_char(parse);
    }

    ch = parse[JSON_DECODE_PARSE_TEXT][parse[JSON_DECODE_PARSE_POS]];
    if (ch < '0' || ch > '9') {
        json_decode_parse_error(parse, "Invalid number", ch);
    }

    while (1) {
        ch = parse[JSON_DECODE_PARSE_TEXT][parse[JSON_DECODE_PARSE_POS]];
        if (ch >= '0' && ch <= '9') {
            json_decode_parse_next_char(parse);
            continue;
        }
        break;
    }

    ch = parse[JSON_DECODE_PARSE_TEXT][parse[JSON_DECODE_PARSE_POS]];
    if (ch == '.') {
        has_dot = 1;
        json_decode_parse_next_char(parse);
        ch = parse[JSON_DECODE_PARSE_TEXT][parse[JSON_DECODE_PARSE_POS]];
        if (ch < '0' || ch > '9') {
            json_decode_parse_error(parse, "Invalid number", ch);
        }
        while (1) {
            ch = parse[JSON_DECODE_PARSE_TEXT][parse[JSON_DECODE_PARSE_POS]];
            if (ch >= '0' && ch <= '9') {
                json_decode_parse_next_char(parse);
                continue;
            }
            break;
        }
    }

    ch = parse[JSON_DECODE_PARSE_TEXT][parse[JSON_DECODE_PARSE_POS]];
    if (ch == 'e' || ch == 'E') {
        has_exp = 1;
        json_decode_parse_next_char(parse);
        ch = parse[JSON_DECODE_PARSE_TEXT][parse[JSON_DECODE_PARSE_POS]];
        if (ch == '+' || ch == '-') {
            json_decode_parse_next_char(parse);
        }
        ch = parse[JSON_DECODE_PARSE_TEXT][parse[JSON_DECODE_PARSE_POS]];
        if (ch < '0' || ch > '9') {
            json_decode_parse_error(parse, "Invalid number", ch);
        }
        while (1) {
            ch = parse[JSON_DECODE_PARSE_TEXT][parse[JSON_DECODE_PARSE_POS]];
            if (ch >= '0' && ch <= '9') {
                json_decode_parse_next_char(parse);
                continue;
            }
            break;
        }
    }

    text = parse[JSON_DECODE_PARSE_TEXT][start..parse[JSON_DECODE_PARSE_POS]-1];
    if (has_dot || has_exp) {
        if (sscanf(text, "%f", float_value) == 1) {
            return float_value;
        }
        return 0.0;
    }

    if (sscanf(text, "%d", int_value) == 1) {
        return int_value;
    }
    return 0;
}

private mixed json_decode_parse_value(mixed* parse) {
    int ch;

    ch = parse[JSON_DECODE_PARSE_TEXT][parse[JSON_DECODE_PARSE_POS]];
    if (!ch) {
        json_decode_parse_error(parse, "Unexpected end of data");
    }

    switch (ch) {
    case '{':
        return json_decode_parse_object(parse);
    case '[':
        return json_decode_parse_array(parse);
    case '"':
        return json_decode_parse_string(parse, 1);
    case 't':
        if (!json_decode_parse_at_token(parse, "true", 0)) {
            json_decode_parse_error(parse, "Invalid token", ch);
        }
        json_decode_parse_next_chars(parse, 4);
        return 1;
    case 'f':
        if (!json_decode_parse_at_token(parse, "false", 0)) {
            json_decode_parse_error(parse, "Invalid token", ch);
        }
        json_decode_parse_next_chars(parse, 5);
        return 0;
    case 'n':
        if (!json_decode_parse_at_token(parse, "null", 0)) {
            json_decode_parse_error(parse, "Invalid token", ch);
        }
        json_decode_parse_next_chars(parse, 4);
        return 0;
    default:
        if (ch == '-' || (ch >= '0' && ch <= '9')) {
            return json_decode_parse_number(parse);
        }
        json_decode_parse_error(parse, "Unexpected character", ch);
    }

    return 0;
}

private string json_encode_replace(string text, string pattern, string replacement) {
    string replaced;

    if (!stringp(text)) {
        return "";
    }

    replaced = replace_string(text, pattern, replacement);
    return stringp(replaced) ? replaced : text;
}

/* 转义控制字符（< 0x20）与控制字符以外的 JSON 必需转义。
 * 反斜杠必须最先处理，否则后续替换会二次转义。 */
private string json_encode_escape_controls(string text) {
    string out;
    int i;
    int len;
    int codepoint;

    out = "";
    len = strlen(text);
    for (i = 0; i < len; i++) {
        codepoint = text[i];
        if (intp(codepoint) && codepoint < 0x20) {
            switch (codepoint) {
            case '\b': out += "\\b"; break;
            case 0x0c: out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:    out += sprintf("\\u%04x", codepoint); break;
            }
            continue;
        }
        out += sprintf("%c", codepoint);
    }

    return out;
}

private string json_encode_escape(string text) {
    string out;

    if (!stringp(text)) {
        return "";
    }

    out = json_encode_replace(text, "\\", "\\\\");
    out = json_encode_replace(out, "\"", "\\\"");
    out = json_encode_escape_controls(out);
    return out;
}

private string json_encode_value(mixed value, int depth) {
    string out;
    int index;
    string *keys;

    if (depth > MAX_JSON_NESTING_DEPTH) {
        error("json_encode: Maximum nesting depth exceeded");
    }

    if (mapp(value)) {
        keys = keys(value);
        out = "{";
        for (index = 0; index < sizeof(keys); index++) {
            string key = keys[index];
            mixed entry = value[key];
            if (index > 0) {
                out += ",";
            }
            out += sprintf("\"%s\":%s", json_encode_escape(key), json_encode_value(entry, depth + 1));
        }
        out += "}";
        return out;
    }

    if (arrayp(value)) {
        out = "[";
        for (index = 0; index < sizeof(value); index++) {
            if (index > 0) {
                out += ",";
            }
            out += json_encode_value(value[index], depth + 1);
        }
        out += "]";
        return out;
    }

    if (stringp(value)) {
        return sprintf("\"%s\"", json_encode_escape(value));
    }

    if (intp(value)) {
        return sprintf("%d", value);
    }

    if (floatp(value)) {
        return sprintf("%f", value);
    }

    if (objectp(value)) {
        return sprintf("\"%s\"", json_encode_escape(file_name(value)));
    }

    return "null";
}

/**
 * @brief Decode a JSON string into an LPC value
 * @param text The JSON string to decode
 * @return The decoded LPC value (mapping, array, string, int, or float)
 */
mixed json_decode(string text) {
    mixed* parse;
    mixed result;
    int len;

    if(!stringp(text) || text == "") {
        error("json_decode: Invalid or empty input");
    }

    parse = allocate(JSON_DECODE_PARSE_FIELDS);
    parse[JSON_DECODE_PARSE_TEXT] = text;
    parse[JSON_DECODE_PARSE_POS] = -1;
    parse[JSON_DECODE_PARSE_LINE] = 1;
    parse[JSON_DECODE_PARSE_CHAR] = 0;
    parse[JSON_DECODE_PARSE_DEPTH] = 0;  // Initialize nesting depth

    json_decode_skip_whitespaces(parse);
    len = strlen(text);
    if (parse[JSON_DECODE_PARSE_POS] >= len) {
        return result;
    }
    result = json_decode_parse_value(parse);
    json_decode_skip_whitespaces(parse);

    if (parse[JSON_DECODE_PARSE_POS] < len && parse[JSON_DECODE_PARSE_TEXT][parse[JSON_DECODE_PARSE_POS]]) {
        json_decode_parse_error(parse, "Unexpected data after JSON value");
    }

    return result;
}

/**
 * @brief Encode an LPC value into a JSON string
 * @param value The LPC value to encode
 * @return The JSON string representation
 */
string json_encode(mixed value) {
    return json_encode_value(value, 0);
}
