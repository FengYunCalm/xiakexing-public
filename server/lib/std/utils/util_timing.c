/**
 * @file    util_timing.c
 * @brief   计时累加器工具 - Timing Accumulator Utilities
 *
 * 统一 wall/cpu 计时累加键形，消灭各 record 函数中的复制粘贴累加块。
 *
 * 键形契约（status 查询消费方依赖，不得更改）：
 * - wall 组：wall_samples / wall_total_us / wall_max_us
 * - cpu 组：<prefix>_samples / <prefix>_total_us / <prefix>_max_us；
 *   cpu_us < 0 时改为累加 <prefix>_unavailable（prefix 缺省 "cpu"，
 *   login_session 压测键形使用 "thread_cpu"）
 * - 简单组：samples / total_us / max_us
 *
 * 语义说明：wall_us < 0 时拒绝累加（perf_counter_ns() 差值单调非负；
 * 拒绝优于让负值污染 total），与各 record 函数既有的 wall_ns < 0 卫语句
 * 意图一致。参数声明为 int（strict_types）：全部既有调用方传入
 * thread_cpu_time_ns()/perf_counter_ns() 差值，均为 int。
 */

#pragma strict_types
#pragma save_types

varargs void accumulate_wall_cpu(mapping store, string key, int wall_us,
    int cpu_us, string cpu_prefix)
{
    mapping timing;
    string prefix;

    if (!mapp(store) || !stringp(key) || key == ""
        || !intp(wall_us) || wall_us < 0 || !intp(cpu_us))
        return;

    timing = mapp(store[key]) ? store[key] : ([]);
    timing["wall_samples"] = (timing["wall_samples"] || 0) + 1;
    timing["wall_total_us"] = (timing["wall_total_us"] || 0) + wall_us;
    if (wall_us > (timing["wall_max_us"] || 0))
        timing["wall_max_us"] = wall_us;

    prefix = stringp(cpu_prefix) && cpu_prefix != "" ? cpu_prefix : "cpu";
    if (cpu_us < 0) {
        timing[prefix + "_unavailable"] =
            (timing[prefix + "_unavailable"] || 0) + 1;
    } else {
        timing[prefix + "_samples"] = (timing[prefix + "_samples"] || 0) + 1;
        timing[prefix + "_total_us"] = (timing[prefix + "_total_us"] || 0) + cpu_us;
        if (cpu_us > (timing[prefix + "_max_us"] || 0))
            timing[prefix + "_max_us"] = cpu_us;
    }
    store[key] = timing;
}

void accumulate_samples(mapping store, string key, int elapsed_us)
{
    mapping timing;

    if (!mapp(store) || !stringp(key) || key == ""
        || !intp(elapsed_us) || elapsed_us < 0)
        return;

    timing = mapp(store[key]) ? store[key] : ([]);
    timing["samples"] = (timing["samples"] || 0) + 1;
    timing["total_us"] = (timing["total_us"] || 0) + elapsed_us;
    if (elapsed_us > (timing["max_us"] || 0))
        timing["max_us"] = elapsed_us;
    store[key] = timing;
}
