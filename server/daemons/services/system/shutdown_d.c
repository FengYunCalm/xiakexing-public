/**
 * @file    shutdown_d.c
 * @brief   服务器关闭服务真源



 *
 * @par Description:
 * 系统级服务器关闭真源，统一负责即时关闭、延迟关闭与取消关闭。
 */

#pragma strict_types
#pragma save_types

#include <system/ansi.h>
#include <system/services.h>
#include <framework/message_facade.h>

inherit "/lib/framework/daemon";

nosave int _shutdown_scheduled_at;
nosave int _shutdown_call_out_handle;
nosave int _driver_shutdown_prepared;
nosave int _driver_shutdown_preparing;

private string format_shutdown_delay(int delay_seconds)
{
    int minutes;

    if (!intp(delay_seconds) || delay_seconds <= 0)
        return "0秒";

    if (delay_seconds % 60 == 0) {
        minutes = delay_seconds / 60;
        return sprintf("%d分钟", minutes);
    }

    return sprintf("%d秒", delay_seconds);
}

private string query_shutdown_actor_id(object actor)
{
    return objectp(actor) ? (get_object_id(actor) || "system") : "system";
}

private mapping save_all_users()
{
    int failed;
    int pending;
    int unknown;
    int saved;
    mapping phase;
    mixed err;
    object user;

    foreach (user in users()) {
        if (!objectp(user) || !function_exists("save_phases", user)) {
            failed++;
            log_file("admin/shutdown", sprintf(
                "[%s] failed to save %s during driver shutdown: save phases unavailable\n",
                ctime(time()), objectp(user) ? file_name(user) : "<invalid>"));
            continue;
        }

        err = catch {
            phase = user->save_phases();
        };
        if (err || !mapp(phase)
            || phase["outcome"] != "committed") {
            failed++;
            if (!err && mapp(phase)) {
                if (phase["outcome"] == "persisted_receipt_pending")
                    pending++;
                else if (phase["outcome"] == "unknown")
                    unknown++;
            }
            log_file("admin/shutdown", sprintf(
                "[%s] failed to save %s during driver shutdown phase=%O err=%O\n",
                ctime(time()), file_name(user),
                err ? "unknown" : phase, err));
            continue;
        }

        saved++;
    }

    return ([ "saved": saved, "failed": failed,
        "pending": pending, "unknown": unknown ]);
}

int prepare_for_driver_shutdown(int exit_code)
{
    mapping result;

    if (_driver_shutdown_prepared)
        return 1;
    if (_driver_shutdown_preparing)
        return 0;

    _driver_shutdown_preparing = 1;
    /* Test fixtures are deliberately non-persistent and may have no session;
     * close them before saving users so shutdown cannot leave world objects or
     * owner references behind. */
    catch(TEST_FIXTURE_D->cleanup_all_fixtures("driver_shutdown"));
    result = save_all_users();
    /* 优雅关停：XK 协议 seq 高水位精确落盘（部署/重启路径无崩溃回退窗口）。 */
    {
        int protocol_saved;

        protocol_saved = 0;
        catch(protocol_saved = call_other("/kernel/framework/xk_protocol_core",
            "persist_protocol_counters"));
        if (!protocol_saved)
            result["failed"]++;
        result["protocol_saved"] = protocol_saved;
    }
    _driver_shutdown_preparing = 0;
    if (result["failed"] == 0)
        _driver_shutdown_prepared = 1;
    log_file("admin/shutdown", sprintf(
        "[%s] prepared driver shutdown exit_code=%d saved=%d failed=%d pending=%d unknown=%d protocol=%d\n",
        ctime(time()), exit_code, result["saved"], result["failed"],
        result["pending"] || 0, result["unknown"] || 0,
        result["protocol_saved"] || 0));
    return result["failed"] == 0;
}

void do_shutdown()
{
    if (!_shutdown_scheduled_at)
        return;

    _shutdown_call_out_handle = 0;
    msg_broadcast(HIR "【系统】服务器正在关闭..." NOR "\n");
    if (!prepare_for_driver_shutdown(0)) {
        log_file("admin/shutdown", sprintf("[%s] shutdown deferred: durable save incomplete\n",
            ctime(time())));
        return;
    }
    shutdown();
}

int cancel_shutdown(object actor)
{
    if (_shutdown_call_out_handle) {
        remove_call_out(_shutdown_call_out_handle);
        _shutdown_call_out_handle = 0;
    }

    _shutdown_scheduled_at = 0;
    msg_broadcast(HIR "【系统】服务器关闭已取消。" NOR "\n");
    log_file("admin/shutdown", sprintf("[%s] %s cancelled server shutdown\n",
        ctime(time()), query_shutdown_actor_id(actor)));
    return 1;
}

int schedule_shutdown(object actor, int delay_seconds)
{
    string delay_text;

    if (!intp(delay_seconds) || delay_seconds <= 0)
        delay_seconds = 300;

    if (_shutdown_call_out_handle) {
        remove_call_out(_shutdown_call_out_handle);
        _shutdown_call_out_handle = 0;
    }

    _shutdown_scheduled_at = time() + delay_seconds;
    delay_text = format_shutdown_delay(delay_seconds);
    msg_broadcast(HIR "【系统】服务器将在" + delay_text + "后关闭。请保存您的进度！" NOR "\n");
    catch(ANNOUNCE_D->publish("服务器维护",
        "服务器将在" + delay_text + "后重启，请及时退出并保存进度。",
        delay_seconds + 60));
    log_file("admin/shutdown", sprintf("[%s] %s scheduled server shutdown in %d seconds\n",
        ctime(time()), query_shutdown_actor_id(actor), delay_seconds));
    _shutdown_call_out_handle = call_out("do_shutdown", delay_seconds);
    return 1;
}

int shutdown_now(object actor)
{
    _shutdown_scheduled_at = time();
    msg_broadcast(HIR "【系统】服务器正在关闭！" NOR "\n");
    log_file("admin/shutdown", sprintf("[%s] %s shutdown server immediately\n",
        ctime(time()), query_shutdown_actor_id(actor)));
    if (!prepare_for_driver_shutdown(0))
        return 0;
    shutdown();
    return 1;
}

int query_shutdown_scheduled_at()
{
    return _shutdown_scheduled_at;
}
