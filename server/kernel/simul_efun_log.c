/**
 * @file    simul_efun_log.c
 * @brief   日志写入辅助子服务
 * @author  XiaKeXing Development Team
 * @date    2026-03-30
 * @version 1.0.0
 */

#pragma strict_types

#include <system/globals.h>
#include <game_constants.h>

// 错误报告明细按前缀保留最近 MAX_ERROR_REPORT_FILES 个，索引超限后轮转；
// 失败只走 debug_message，不回写日志，避免错误处理重入。
#define MAX_ERROR_REPORT_FILES  200

int log_file(string file, string message)
{
    string log_path;
    string dir;
    string *parts;
    string part;
    mixed err;
    int written;
    int i;

    if (!stringp(file) || file == "" || file[0] == '/') {
        debug_message("[log_file] invalid relative log name\n");
        return 0;
    }
    for (i = 0; i < strlen(file); i++) {
        if (file[i] < 32 || file[i] == 127 || file[i] == '\\') {
            debug_message("[log_file] invalid log path character\n");
            return 0;
        }
    }
    parts = explode(file, "/");
    foreach (part in parts) {
        if (part == "" || part == "." || part == "..") {
            debug_message("[log_file] invalid log path component\n");
            return 0;
        }
    }
    if (!stringp(message))
        message = "null";

    log_path = LOG_DIR + file;
    dir = LOG_DIR;
    for (i = 0; i < sizeof(parts); i++) {
        if (file_size(dir) != -2) {
            err = catch(written = mkdir(dir));
            if (err || !written) {
                debug_message(sprintf("[log_file] cannot create %s: %O\n", dir, err));
                return 0;
            }
        }
        dir += parts[i] + "/";
    }

    err = catch(written = write_file(log_path, message));
    if (err || !written) {
        debug_message(sprintf("[log_file] cannot append %s: %O\n", log_path, err));
        return 0;
    }
    return 1;
}

private int is_all_digits(string text)
{
    int i;

    if (!stringp(text) || text == "")
        return 0;
    for (i = 0; i < strlen(text); i++) {
        if (text[i] < '0' || text[i] > '9')
            return 0;
    }
    return 1;
}

// 只清理本 helper 自己生成的 <prefix>-<时间>-<序号>.log，其他文件不拥有。
private int is_owned_error_report(string prefix, string name)
{
    string *parts;
    string serial;

    parts = explode(name, "-");
    if (sizeof(parts) != 3 || parts[0] != prefix)
        return 0;
    serial = parts[2];
    if (strlen(serial) <= 4 || serial[<4..] != ".log")
        return 0;
    return is_all_digits(parts[1]) && is_all_digits(serial[0..<5]);
}

private int prune_error_reports(string prefix)
{
    string *names;
    string *owned;
    string name;
    int stale;
    int i;

    names = get_dir(LOG_DIR);
    if (!arrayp(names))
        return 0;

    owned = ({ });
    foreach (name in names) {
        if (is_owned_error_report(prefix, name))
            owned += ({ name });
    }
    stale = sizeof(owned) - MAX_ERROR_REPORT_FILES;
    if (stale <= 0)
        return 0;

    owned = sort_array(owned, 1);
    for (i = 0; i < stale; i++) {
        if (!rm(LOG_DIR + owned[i]))
            debug_message(sprintf("[error_report] cannot drop %s\n", LOG_DIR + owned[i]));
    }
    return stale;
}

/**
 * @brief 写入错误报告明细并追加索引行
 * @param prefix 报告前缀（runtime/compile），只接受单一目录名
 * @param text 明细内容
 * @return 明细文件路径；"" 表示明细未落盘
 */
string write_error_report(string prefix, string text)
{
    string index_log;
    string backup_log;
    string detail_log;
    mixed err;
    int written;

    if (!stringp(prefix) || prefix == "" || !stringp(text))
        return "";
    if (strsrch(prefix, "/") != -1 || prefix == "." || prefix == "..") {
        debug_message("[error_report] invalid report prefix\n");
        return "";
    }

    if (file_size(LOG_DIR) != -2 && !mkdir(LOG_DIR))
        debug_message(sprintf("[error_report] cannot create %s\n", LOG_DIR));

    detail_log = sprintf("%s%s-%d-%05d.log", LOG_DIR, prefix, time(), random(100000));
    err = catch(written = write_file(detail_log, text));
    if (err || !written) {
        debug_message(sprintf("[error_report] cannot write detail %s: %O\n", detail_log, err));
        return "";
    }

    index_log = LOG_DIR + prefix + ".log";
    backup_log = LOG_DIR + prefix + ".bak";
    if (file_size(index_log) > MAX_LOG_FILE_SIZE) {
        err = catch(written = rename(index_log, backup_log));
        if (err || written != 0) {
            debug_message(sprintf("[error_report] cannot rotate %s: %O\n", index_log, err));
            return detail_log;
        }
    }
    err = catch(written = write_file(index_log, sprintf("[%s] %s\n", ctime(time()), detail_log)));
    if (err || !written)
        debug_message(sprintf("[error_report] cannot append %s: %O\n", index_log, err));

    prune_error_reports(prefix);
    return detail_log;
}
