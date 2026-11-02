/**
 * @file    time_d.c
 * @brief   游戏时辰系统（世界时间轴）。
 *
 * 一天 = 12 时辰（子丑寅卯辰巳午未申酉戌亥），
 * 起始配置：1 现实分钟 = 1 游戏时辰（24 分钟一天）。
 * 游戏时间持久化（重启不归零），时辰推进驱动昼夜状态与
 * 世界钩子（商店打烊 / NPC 日程 / 世界事件）。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>
#include <framework/message_facade.h>

inherit "/lib/framework/daemon";

#define TIME_SAVE_FILE     "/data/game_time"
#define MINUTES_PER_HOUR   1
#define HOURS_PER_DAY      12
#define PERIOD_DAWN        "dawn"
#define PERIOD_MORNING     "morning"
#define PERIOD_NOON        "noon"
#define PERIOD_DUSK        "dusk"
#define PERIOD_NIGHT       "night"

nosave int _hour_of_day;
nosave int _day_count;
nosave string _save_file;
nosave int _tick_handle;
nosave int _persist_pending;
nosave int _pending_hour;
nosave int _pending_day;
nosave int _persist_retry_count;
nosave int _persist_retry_handle;
nosave string _last_persist_result;
nosave string _last_persist_error;

private void restore_game_time();
private int save_game_time();
private void schedule_next_tick();
private int publish_time_candidate(int hour, int day);
private void retry_pending_time();
private void schedule_time_retry();
string query_period();
void advance_one_hour();

string *query_hour_names()
{
    return ({ "子", "丑", "寅", "卯", "辰", "巳", "午", "未", "申", "酉", "戌", "亥" });
}

void create()
{
    ::create();
    _save_file = TIME_SAVE_FILE;
    _persist_pending = 0;
    _persist_retry_count = 0;
    _persist_retry_handle = 0;
    _last_persist_result = "not_attempted";
    _last_persist_error = "";
    restore_game_time();
    schedule_next_tick();
    /* 加载自主运行的兄弟 daemon（无人调用则不加载） */
    catch(load_object(NPC_SCHEDULE_D));
    catch(load_object(WORLD_EVENT_D));
}

private void schedule_next_tick()
{
    if (_tick_handle)
        remove_call_out(_tick_handle);
    _tick_handle = call_out("advance_one_hour", MINUTES_PER_HOUR * 60);
}

void advance_one_hour()
{
    int next_hour;
    int next_day;

    _tick_handle = 0;
    if (_persist_pending) {
        retry_pending_time();
        schedule_next_tick();
        return;
    }
    next_hour = (_hour_of_day + 1) % HOURS_PER_DAY;
    next_day = _day_count + (next_hour == 0 ? 1 : 0);
    publish_time_candidate(next_hour, next_day);
    schedule_next_tick();
}

int query_hour()
{
    return _hour_of_day;
}

int query_hour_at(int epoch)
{
    if (!intp(epoch) || epoch < 0)
        return 0;
    return (epoch / (MINUTES_PER_HOUR * 60)) % HOURS_PER_DAY;
}

mapping query_persistence_status()
{
    return ([
        "hour": _hour_of_day,
        "day": _day_count,
        "persist_pending": _persist_pending,
        "last_persist_result": _last_persist_result,
        "last_persist_error": _last_persist_error,
        "retry_count": _persist_retry_count,
    ]);
}

/* admin 调试/运营：直接设定时辰（会触发世界钩子） */
int set_hour(int hour)
{
    if (!intp(hour) || hour < 0 || hour >= HOURS_PER_DAY || _persist_pending)
        return 0;
    return publish_time_candidate(hour, _day_count);
}

/* 十二时辰制的真源：日程/内容配置的 hour 一律按这个范围校验，
 * 不再各自写 0..23。 */
int query_hours_per_day()
{
    return HOURS_PER_DAY;
}

int query_day()
{
    return _day_count;
}

int set_day(int day)
{
    if (!intp(day) || day < 0 || _persist_pending)
        return 0;
    return publish_time_candidate(_hour_of_day, day);
}

string query_hour_name()
{
    return query_hour_names()[_hour_of_day % HOURS_PER_DAY];
}

string query_period()
{
    int hour;

    hour = _hour_of_day;
    if (hour >= 1 && hour < 4)
        return PERIOD_DAWN;
    if (hour >= 4 && hour < 8)
        return PERIOD_MORNING;
    if (hour >= 8 && hour < 10)
        return PERIOD_NOON;
    if (hour >= 10 && hour < 12)
        return PERIOD_DUSK;
    return PERIOD_NIGHT;
}

string query_period_text()
{
    string period;

    period = query_period();
    if (period == PERIOD_DAWN)
        return "拂晓";
    if (period == PERIOD_MORNING)
        return "日间";
    if (period == PERIOD_NOON)
        return "正午";
    if (period == PERIOD_DUSK)
        return "黄昏";
    return "夜里";
}

/* 店铺是否开门：open_hours = ({ 开门时辰, 关门时辰 })，闭区间循环 */
int is_shop_open(mapping shop)
{
    int *hours;
    int hour;
    int open_at;
    int close_at;

    if (!mapp(shop))
        return 1;
    hours = shop["open_hours"];
    if (!arrayp(hours) || sizeof(hours) != 2)
        return 1;

    open_at = hours[0];
    close_at = hours[1];
    hour = _hour_of_day;
    if (open_at == close_at)
        return 1;
    if (open_at < close_at)
        return hour >= open_at && hour < close_at;
    return hour >= open_at || hour < close_at;
}

/* 房间描述的昼夜氛围句（look 房间时追加一行）。
 * 世界级通用机制：按房间属性（露天水域/山林/城市/室内）分层出句，
 * 房间自身不承担任何时间逻辑。
 */
private string ambience_water(string period)
{
    switch (period) {
    case PERIOD_DAWN:
        return "水面浮着一层薄雾，渔火还没熄，远处传来一两声橹响。\n";
    case PERIOD_MORNING:
        return "水汽被日头蒸起来，波光晃得人睁不开眼，船家的吆喝声顺着风传来。\n";
    case PERIOD_NOON:
        return "日头晒得水面发白，芦苇丛里静得出奇，只有水鸟偶尔扑棱一下。\n";
    case PERIOD_DUSK:
        return "水面染成一片金红色，归舟的帆影一个接一个靠岸。\n";
    default:
        return "水面黑沉沉的，只有几点渔火忽明忽灭，像是漂在水上的星星。\n";
    }
}

private string ambience_mountain(string period)
{
    switch (period) {
    case PERIOD_DAWN:
        return "晨雾顺着山脊流淌，鸟鸣一声接一声地醒了过来。\n";
    case PERIOD_MORNING:
        return "林间漏下碎金似的日光，山道上落着隔夜的松针。\n";
    case PERIOD_NOON:
        return "山风裹着草木的燥气，蝉声像锯子一样拉着。\n";
    case PERIOD_DUSK:
        return "山色一层层暗下去，归鸟的影子掠过林梢。\n";
    default:
        return "林间黑得伸手不见五指，偶尔有猫头鹰的叫声从深处传来。\n";
    }
}

private string ambience_city(string period)
{
    switch (period) {
    case PERIOD_DAWN:
        return "城门刚开，早市的摊子还没支全，街面上稀稀落落几个赶早的人。\n";
    case PERIOD_MORNING:
        return "街市上人声渐起，货郎的拨浪鼓和讨价还价声混成一片。\n";
    case PERIOD_NOON:
        return "日头正毒，街上的人少了大半，檐下的阴凉里蹲着打盹的狗。\n";
    case PERIOD_DUSK:
        return "檐角的灯笼一盏盏亮起来，夜市的热气开始冒头。\n";
    default:
        return "更鼓敲过三更，石板路上只剩巡夜人的脚步声。\n";
    }
}

private string ambience_indoor(string period)
{
    switch (period) {
    case PERIOD_DAWN:
        return "屋里还黑着，窗外透进来一线灰白的光。\n";
    case PERIOD_MORNING:
        return "晨光从窗棂漏进来，尘埃在光柱里慢慢浮着。\n";
    case PERIOD_NOON:
        return "日头从窗缝里挤进来一束，照得灰尘无处可藏。\n";
    case PERIOD_DUSK:
        return "屋里暗下来了，有人摸出火折子点亮了油灯。\n";
    default:
        return "油灯昏黄，墙上的人影随着灯焰轻轻摇晃。\n";
    }
}

int contains_any(string text, string *keys);

int contains_any(string text, string *keys)
{
    string key;

    if (!stringp(text) || text == "")
        return 0;
    foreach (key in keys)
        if (strsrch(text, key) != -1)
            return 1;
    return 0;
}

string query_ambience_line_for(mapping room_info);

string query_ambience_line()
{
    return query_ambience_line_for(([ ]));
}

/* 按房间属性分层出句：room_info 可含 outdoors / title */
string query_ambience_line_for(mapping room_info)
{
    string period;
    string title;
    int outdoors;

    period = query_period();
    title = mapp(room_info) ? (room_info["title"] || "") : "";
    outdoors = mapp(room_info) ? intp(room_info["outdoors"]) && room_info["outdoors"] : 1;

    if (!outdoors)
        return ambience_indoor(period);
    if (contains_any(title, ({ "湖", "河", "江", "海", "塘", "渡", "苇", "荷", "芦", "洲",
        "滩", "湾", "荡", "池", "渠", "岸", "溪", "淀", "汊" })))
        return ambience_water(period);
    if (contains_any(title, ({ "山", "峰", "岭", "崖", "岗", "坡", "林", "松", "竹",
        "谷", "涧", "坪", "麓", "顶" })))
        return ambience_mountain(period);
    if (contains_any(title, ({ "城", "街", "市", "门", "桥", "关", "镇", "亭", "台", "庙",
        "寺", "观", "祠", "塔", "楼", "驿", "馆", "铺", "堂" })))
        return ambience_city(period);
    switch (period) {
    case PERIOD_DAWN:
        return "天边刚泛白，晨雾还挂在屋檐下。\n";
    case PERIOD_MORNING:
        return "日头升起来了，街上的人渐渐多了。\n";
    case PERIOD_NOON:
        return "正午的日头毒，晒得石板路发白。\n";
    case PERIOD_DUSK:
        return "天色暗下来，檐角的灯笼陆续亮起。\n";
    default:
        return "夜里静悄悄的，月光把影子拉得很长。\n";
    }
}

string query_full_time_text()
{
    return sprintf("第%d日，%s时（%s）", _day_count + 1, query_hour_name(), query_period_text());
}

private void restore_game_time()
{
    string raw;
    mapping data;
    mixed err;
    int saved_at;

    raw = read_file(_save_file);
    if (!stringp(raw) || raw == "")
        return;
    err = catch(data = restore_variable(raw));
    if (err || !mapp(data)) {
        _last_persist_result = "restore_failed";
        _last_persist_error = err ? sprintf("%O", err) : "time snapshot is not a mapping";
        return;
    }
    saved_at = intp(data["saved_at"]) ? data["saved_at"] : time();
    _day_count = intp(data["day"]) && data["day"] >= 0 ? data["day"] : 0;
    _hour_of_day = intp(data["hour"]) && data["hour"] >= 0
        ? data["hour"] % HOURS_PER_DAY : 0;
    /* 离线流逝：按现实分钟折算时辰 */
    if (saved_at > 0 && saved_at < time()) {
        int elapsed_minutes;

        elapsed_minutes = (time() - saved_at) / 60;
        if (elapsed_minutes > 0) {
            int hours_elapsed;
            int total_hours;

            hours_elapsed = elapsed_minutes / MINUTES_PER_HOUR;
            /* L-64：必须按**绝对累计小时**回算日与时辰。
             * 旧实现把小时取模写回、天数只加 hours/12，跨日进位会丢
             * （hour=8 + 5 小时 => 应进一天，旧算法不进）。 */
            total_hours = _day_count * HOURS_PER_DAY + _hour_of_day
                + hours_elapsed;
            _day_count = total_hours / HOURS_PER_DAY;
            _hour_of_day = total_hours % HOURS_PER_DAY;
        }
    }
}

private int publish_time_candidate(int hour, int day)
{
    int previous_hour;
    int previous_day;
    int saved;

    if (_persist_pending)
        return 0;
    previous_hour = _hour_of_day;
    previous_day = _day_count;
    _hour_of_day = hour;
    _day_count = day;
    saved = save_game_time();
    if (saved) {
        EVENT_D->emit("game.time.hour_changed", ([
            "hour": _hour_of_day,
            "day": _day_count,
            "period": query_period(),
        ]));
        return 1;
    }
    _pending_hour = hour;
    _pending_day = day;
    _persist_pending = 1;
    _hour_of_day = previous_hour;
    _day_count = previous_day;
    _persist_retry_count = 0;
    schedule_time_retry();
    return 0;
}

private void schedule_time_retry()
{
    if (_persist_retry_handle || !_persist_pending || _persist_retry_count >= 3)
        return;
    _persist_retry_handle = call_out("retry_pending_time", 5);
}

private void retry_pending_time()
{
    int previous_hour;
    int previous_day;
    int saved;

    _persist_retry_handle = 0;
    if (!_persist_pending)
        return;
    previous_hour = _hour_of_day;
    previous_day = _day_count;
    _hour_of_day = _pending_hour;
    _day_count = _pending_day;
    _persist_retry_count++;
    saved = save_game_time();
    if (saved) {
        _persist_pending = 0;
        _persist_retry_count = 0;
        EVENT_D->emit("game.time.hour_changed", ([
            "hour": _hour_of_day,
            "day": _day_count,
            "period": query_period(),
            "recovered": 1,
        ]));
        return;
    }
    _hour_of_day = previous_hour;
    _day_count = previous_day;
    schedule_time_retry();
}

private int save_game_time()
{
    mapping data;
    string content;
    string temp_file;
    string backup_file;
    mixed err;

    data = ([
        "saved_at": time(),
        "day": _day_count,
        "hour": _hour_of_day,
    ]);
    err = catch(content = save_variable(data));
    if (err || !stringp(content)) {
        _last_persist_result = "failed";
        _last_persist_error = err ? sprintf("%O", err) : "save_variable failed";
        return 0;
    }
    temp_file = _save_file + ".tmp";
    backup_file = _save_file + ".bak";
    rm(temp_file);
    if (!write_file(temp_file, content, 1)) {
        _last_persist_result = "failed";
        _last_persist_error = "temporary time snapshot write failed";
        return 0;
    }
    rm(backup_file);
    if (file_size(_save_file) >= 0 && rename(_save_file, backup_file) != 0) {
        rm(temp_file);
        _last_persist_result = "failed";
        _last_persist_error = "time snapshot backup rename failed";
        return 0;
    }
    if (rename(temp_file, _save_file) != 0) {
        if (file_size(backup_file) >= 0)
            rename(backup_file, _save_file);
        _last_persist_result = "failed";
        _last_persist_error = "time snapshot publish rename failed";
        return 0;
    }
    rm(backup_file);
    _last_persist_result = "success";
    _last_persist_error = "";
    return 1;
}
