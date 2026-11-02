/**
 * @file    auction_d.c
 * @brief   拍卖守护进程,管理拍卖挂单、序列与收盘回调。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

inherit "/lib/framework/daemon";

int queue_compensation_with_operation(object who, string domain, string action,
    string actor_id, string subject_id, string operation_id, int amount);

mapping _listings;
int _listing_seq;
nosave mapping _close_handles;
nosave mapping _last_auction_save_phase;
/* A durable auction candidate with an open owner receipt blocks every new
 * candidate on the same file until the original receipt is recovered. */
nosave mapping _auction_save_receipt_pending;
/* An existing invalid auction ledger is recovery-pending, not an empty
 * market. Refuse all writes until the file is repaired. */
nosave int _auction_state_status;
nosave string _auction_state_error;

private string get_auction_save_file()
{
    return DATA_DIR + "runtime/auction_d";
}

private void restore_auction_data()
{
    string file;
    mixed err;
    int restored;
    int size;

    _auction_state_status = 0;
    _auction_state_error = "";
    file = get_auction_save_file();
    size = file_size(file + ".o");
    if (size == -1) {
        _listings = ([]);
        _listing_seq = 0;
        _auction_state_status = 1;
        return;
    }
    if (size <= 0) {
        _listings = ([]);
        _listing_seq = 0;
        _auction_state_error = "auction state file is empty or not regular";
        return;
    }

    err = catch(restored = restore_object(file));
    if (err || !restored || !mapp(_listings)) {
        _listings = ([]);
        _listing_seq = 0;
        _auction_state_error = err ? sprintf("%O", err)
            : !restored ? "auction state restore failed"
            : "auction state shape invalid";
        return;
    }

    if (!intp(_listing_seq) || _listing_seq < 0)
        _listing_seq = 0;
    _auction_state_status = 1;
}

private int auction_state_ready()
{
    return _auction_state_status == 1 && mapp(_listings);
}

private int retry_auction_save_receipt()
{
    mapping pending;
    mapping snapshot;
    mapping result;
    mapping retired;
    mixed err;
    mixed retire_error;
    int snapshot_id;

    pending = mapp(_auction_save_receipt_pending)
        ? _auction_save_receipt_pending : 0;
    if (!mapp(pending)) {
        err = catch(result = OWNER_PERSISTENCE_D->query_pending_publication(
            "owner/global/service/economy/auction", "auction_state",
            get_auction_save_file() + ".o"));
        if (err || !mapp(result) || result["ok"] != 1) {
            _last_auction_save_phase = ([
                "outcome": "unknown", "persisted": -1,
                "save_result": 0,
                "error": err ? sprintf("%O", err)
                    : (mapp(result) ? result["error_code"]
                    : "auction publication lookup failed"),
            ]);
            return 0;
        }
        snapshot = result["snapshot"];
        if (!mapp(snapshot))
            return 1;
        pending = ([ "snapshot_id": snapshot["id"],
            "retry_token": "snapshot-retry/" + snapshot["id"] ]);
        _auction_save_receipt_pending = pending;
    }
    snapshot_id = intp(pending["snapshot_id"]) ? pending["snapshot_id"] : 0;
    if (snapshot_id <= 0) {
        _last_auction_save_phase = ([
            "outcome": "unknown", "persisted": -1,
            "save_result": 0, "error": "auction receipt identity missing",
        ]);
        return 0;
    }
    err = catch(result = OWNER_PERSISTENCE_D->recover_snapshot_publication(
        snapshot_id));
    if (err || !mapp(result)) {
        _last_auction_save_phase = ([
            "outcome": "unknown", "persisted": -1,
            "save_result": 0, "error": err ? sprintf("%O", err)
                : "auction publication recovery unavailable",
        ]);
        return 0;
    }
    if (result["outcome"] == "not_written") {
        retire_error = catch(retired =
            OWNER_PERSISTENCE_D->retire_unwritten_snapshot(snapshot_id,
                "publication evidence proves auction candidate was not written"));
        if (retire_error || !mapp(retired) || retired["ok"] != 1) {
            _last_auction_save_phase = ([
                "outcome": "unknown", "persisted": -1,
                "save_result": 0,
                "error": retire_error ? sprintf("%O", retire_error)
                    : "auction not-written receipt could not be retired",
            ]);
            return 0;
        }
        _auction_save_receipt_pending = 0;
        _last_auction_save_phase = ([
            "outcome": "not_written", "persisted": 0,
            "save_result": 0,
            "retry_token": pending["retry_token"] || "",
        ]);
        return 1;
    }
    if (result["ok"] != 1 || result["outcome"] != "committed") {
        _last_auction_save_phase = ([
            "outcome": result["outcome"] == "unknown"
                ? "unknown" : "persisted_receipt_pending",
            "persisted": result["outcome"] == "unknown" ? -1 : 1,
            "save_result": result["outcome"] == "unknown" ? 0 : 2,
            "retry_token": pending["retry_token"] || "",
            "error": result["error_code"] || "auction receipt pending",
        ]);
        return 0;
    }
    _auction_save_receipt_pending = 0;
    _last_auction_save_phase = ([
        "outcome": "committed", "persisted": 1, "save_result": 1,
    ]);
    return 1;
}

private int persist_auction_data()
{
    string file;
    string runtime_dir;
    mapping detail;

    if (!auction_state_ready()) {
        _last_auction_save_phase = ([
            "outcome": "not_written", "persisted": 0,
            "save_result": 0,
            "error": _auction_state_error || "auction state unavailable",
        ]);
        return 0;
    }
    if (!retry_auction_save_receipt())
        return 0;
    file = get_auction_save_file();
    runtime_dir = DATA_DIR + "runtime";
    if (file_size(runtime_dir) == -1)
        mkdir(runtime_dir);
    if (!mapp(_listings))
        _listings = ([]);
    if (!intp(_listing_seq) || _listing_seq < 0)
        _listing_seq = 0;

    detail = owner_save_object_phases(file, "owner/global/service/economy/auction", "auction_state", ([
        "listing_count": sizeof(_listings),
        "listing_seq": _listing_seq,
        "state_digest": hash("sha256", save_variable(_listings)
            + sprintf("/%d", _listing_seq)),
    ]), "auction/save/" + hash("sha256", save_variable(_listings)
        + sprintf("/%d", _listing_seq)));
    _last_auction_save_phase = mapp(detail) ? copy(detail)
        : ([ "outcome": "unknown", "persisted": -1, "save_result": 0,
            "error": "auction owner save result unavailable" ]);
    if (!mapp(detail) || detail["persisted"] != 1)
        return mapp(detail) && detail["outcome"] != "not_written" ? -1 : 0;
    return detail["ok"] == 1 ? 1 : 2;
}

private int save_auction_data()
{
    return persist_auction_data();
}

mapping query_auction_save_phase()
{
    return mapp(_last_auction_save_phase) ? copy(_last_auction_save_phase)
        : ([ "outcome": "not_attempted", "persisted": 0, "save_result": 0 ]);
}

private string classify_auction_save_result(int save_result)
{
    if (save_result == 1)
        return "committed";
    if (mapp(_last_auction_save_phase)
        && stringp(_last_auction_save_phase["outcome"])
        && _last_auction_save_phase["outcome"] != "")
        return _last_auction_save_phase["outcome"];
    if (save_result == 2)
        return "persisted_receipt_pending";
    if (save_result == 0)
        return "not_written";
    return "unknown";
}

private string auction_save_message(string state, string action)
{
    if (state == "persisted_receipt_pending")
        return sprintf("拍卖%s已写入，收据尚未完成，请稍后确认。\\n", action);
    if (state == "not_written")
        return sprintf("拍卖%s未落盘，请稍后使用相同请求重试。\\n", action);
    return sprintf("拍卖%s持久化结果未知，已保留恢复责任，请稍后确认。\\n", action);
}

private string ensure_player_id(object player)
{
    string player_id;

    if (!objectp(player))
        return 0;
    player_id = get_object_id(player);
    return stringp(player_id) && player_id != "" ? player_id : 0;
}

private string query_player_display_name(object player)
{
    string name;

    if (!objectp(player))
        return "玩家";
    name = player->query_name() || player->query("name") || ensure_player_id(player) || "玩家";
    return name;
}

private object find_online_player_by_id(string player_id)
{
    if (!stringp(player_id) || player_id == "")
        return 0;

    return find_player_online(player_id);
}

private mapping ensure_listing(string listing_id)
{
    if (!stringp(listing_id) || !mapp(_listings[listing_id]))
        return 0;
    return _listings[listing_id];
}

private string generate_listing_id()
{
    _listing_seq += 1;
    return sprintf("auction_%d_%d", time(), _listing_seq);
}

private void cancel_close_timer(mapping listing)
{
    int handle;
    mapping timer_entry;
    string listing_id;

    if (!mapp(listing) || !mapp(_close_handles))
        return;

    listing_id = listing["id"] || "";
    timer_entry = _close_handles[listing_id];
    handle = mapp(timer_entry) ? (timer_entry["handle"] || 0) : timer_entry;
    if (intp(handle) && handle > 0
        && (!mapp(timer_entry) || timer_entry["mode"] == "owner_timer"))
        TIMER_D->cancel_timer(handle);
    if (intp(handle) && handle > 0 && mapp(timer_entry)
        && timer_entry["mode"] == "local_fallback")
        remove_call_out(handle);
    map_delete(_close_handles, listing_id);
}

private void schedule_listing_close(mapping listing)
{
    int delay;
    int handle;
    string mode;
    string listing_id;

    if (!mapp(listing))
        return;

    cancel_close_timer(listing);
    if ((listing["status"] || "") != "active")
        return;

    listing_id = listing["id"] || "";
    delay = (listing["close_at"] || time()) - time();
    if (delay < 1)
        delay = 1;
    handle = TIMER_D->call_later("handle_listing_close", delay,
        this_object(), listing_id);
    mode = "owner_timer";
    if (!intp(handle) || handle <= 0) {
        handle = call_out("handle_listing_close", delay, listing_id);
        mode = "local_fallback";
    }
    if (!intp(handle) || handle <= 0) {
        listing["timer_state"] = "unavailable";
        listing["timer_error"] = "owner_route_unavailable";
        save_auction_data();
        return;
    }
    _close_handles[listing_id] = (["handle": handle, "mode": mode]);
    listing["timer_state"] = mode;
    map_delete(listing, "timer_error");
}

private mapping build_attachment_rewards(mapping attachment, int gold)
{
    mapping rewards;

    rewards = ([ "gold": gold > 0 ? gold : 0 ]);
    if (mapp(attachment))
        rewards["attachments"] = ({ copy(attachment) });
    return rewards;
}

private mapping deliver_reward_or_mail(string player_id, mapping rewards,
    string subject, string content, string source)
{
    mapping applied;
    int mail_result;
    object player;

    if (!stringp(player_id) || player_id == "")
        return ([ "ok": 0, "delivery": "invalid" ]);

    player = find_online_player_by_id(player_id);
    if (objectp(player)) {
        applied = REWARD_APPLY_D->apply_reward_package(player, rewards,
            ([ "drop_on_full": 0, "source": source,
                "idempotent": 1,
                "target_key": "auction/" + source ]));
        if (mapp(applied) && applied["ok"])
            return ([ "ok": 1, "delivery": "direct" ]);
        if (mapp(applied) && (applied["pending_recovery"]
            || applied["state"] == "persisted_receipt_pending"
            || applied["state"] == "unknown"))
            return ([ "ok": 0, "delivery": "direct_pending",
                "pending_recovery": 1,
                "save_result": applied["save_result"] || 0 ]);
    }

    MAIL_D->set_mail_operation_id("auction/" + source + "/" + player_id);
    mail_result = MAIL_D->send_system_mail(player_id, subject, content,
        rewards["attachments"] || ({ }), rewards["gold"] || 0);
    MAIL_D->clear_mail_operation_id();
    if (mail_result == 1) {
        return ([ "ok": 1, "delivery": "mail",
            "save_result": mail_result ]);
    }
    if (mail_result == 2) {
        return ([ "ok": 0, "delivery": "mail_pending",
            "save_result": mail_result, "pending_recovery": 1 ]);
    }

    return ([ "ok": 0, "delivery": "pending" ]);
}

private void record_pending_bid_refund(mapping listing, string player_id,
    int amount)
{
    mapping pending;
    string operation_id;

    if (!mapp(listing) || !stringp(player_id) || player_id == ""
        || amount <= 0)
        return;
    operation_id = "auction/" + (listing["id"] || "auction")
        + "/bid/" + player_id + "/" + amount + "/refund";
    pending = mapp(listing["pending_refunds"])
        ? listing["pending_refunds"] : ([]);
    pending[operation_id] = ([
        "player_id": player_id,
        "amount": amount,
        "operation_id": operation_id,
    ]);
    listing["pending_refunds"] = pending;
}

private void attempt_listing_delivery(mapping listing, string role)
{
    mapping delivery;
    mapping rewards;
    string player_id;
    string subject;
    string content;

    if (!mapp(listing) || !stringp(role) || role == "")
        return;
    if (!mapp(listing["pending_rewards"]) || !mapp(listing["pending_rewards"][role]))
        return;
    if (mapp(listing["claimed"]) && listing["claimed"][role])
        return;

    rewards = listing["pending_rewards"][role];
    player_id = role == "seller" ? (listing["seller_id"] || "") : (listing["current_bidder"] || "");
    subject = sprintf("拍卖结算：%s", listing["item_name"] || "拍卖物品");
    content = role == "seller"
        ? "你的拍卖结果已结算，请查收收益。"
        : "你拍中的物品已发放，请查收。";

    delivery = deliver_reward_or_mail(player_id, rewards, subject, content,
        "auction_close/" + (listing["id"] || "auction") + "/" + role);
    if (!mapp(delivery) || !delivery["ok"])
        return;

    if (!mapp(listing["claimed"]))
        listing["claimed"] = ([]);
    if (!mapp(listing["delivery"]))
        listing["delivery"] = ([]);
    listing["claimed"][role] = 1;
    listing["delivery"][role] = delivery["delivery"] || "direct";
}

void schedule_delivery_recovery();

private mapping build_listing_close_result(mapping listing, string status)
{
    return ([
        "success": 1,
        "listing_id": listing["id"] || "",
        "status": status,
        "delivery": copy(listing["delivery"] || ([])),
        "claimed": copy(listing["claimed"] || ([])),
    ]);
}

void handle_listing_close(string listing_id)
{
    mapping listing;
    mapping previous_listing;
    mapping settlement;
    int save_result;

    listing = ensure_listing(listing_id);
    if (!mapp(listing) || (listing["status"] || "") != "active")
        return;

    previous_listing = copy(listing);
    cancel_close_timer(listing);
    settlement = ECONOMY_SETTLEMENT_D->settle_auction_listing_close(listing);
    if (!mapp(settlement) || !settlement["success"]) {
        listing["status"] = "close_failed";
        save_result = save_auction_data();
        if (save_result != 1)
            log_file("auction", sprintf("%s close failure state pending: listing=%s phase=%O\\n",
                ctime(time()), listing_id, query_auction_save_phase()));
        schedule_delivery_recovery();
        return;
    }

    listing["closed_at"] = time();
    listing["settlement"] = settlement;
    listing["pending_rewards"] = ([]);
    listing["claimed"] = ([ "seller": 0, "buyer": 0 ]);
    listing["delivery"] = ([ ]);

    if (!settlement["sold"]) {
        listing["status"] = "closed_unsold";
        listing["pending_rewards"]["seller"] = build_attachment_rewards(
            listing["attachment"], settlement["seller_gold"] || 0);
    } else {
        listing["status"] = "closed_sold";
        listing["pending_rewards"]["seller"] = build_attachment_rewards(
            0, settlement["seller_gold"] || 0);
        listing["pending_rewards"]["buyer"] = build_attachment_rewards(
            listing["attachment"], 0);
    }

    /* Publish the close and its delivery obligations before transferring
     * assets. A not-written close must leave the active listing untouched. */
    save_result = save_auction_data();
    if (save_result == 0) {
        _listings[listing_id] = previous_listing;
        schedule_listing_close(previous_listing);
        return;
    }
    if (save_result != 1) {
        schedule_delivery_recovery();
        return;
    }

    attempt_listing_delivery(listing, "seller");
    if (settlement["sold"])
        attempt_listing_delivery(listing, "buyer");
    save_result = save_auction_data();
    if (save_result != 1)
        log_file("auction", sprintf("%s close delivery state pending: listing=%s phase=%O\\n",
            ctime(time()), listing_id, query_auction_save_phase()));
    schedule_delivery_recovery();
}

mapping query_runtime_snapshot()
{
    return ([
        "listing_count": sizeof(keys(_listings || ([]))),
        "active_count": sizeof(filter(keys(_listings || ([])), (: mapp(_listings[$1]) && _listings[$1]["status"] == "active" :))),
        "auction_state_loaded": auction_state_ready() ? 1 : 0,
        "auction_state_error": _auction_state_error || "",
        "runtime": ECONOMY_SETTLEMENT_D->query_auction_runtime_snapshot(),
    ]);
}

mapping query_listing(string listing_id)
{
    mapping listing;

    listing = ensure_listing(listing_id);
    return mapp(listing) ? copy(listing) : 0;
}

mixed *query_public_listings()
{
    mixed *listings;
    mapping listing;

    listings = ({ });
    foreach (listing in values(_listings || ([]))) {
        if (!mapp(listing))
            continue;
        listings += ({ copy(listing) });
    }

    return sort_array(listings, (: ($1["created_at"] || 0) > ($2["created_at"] || 0) ? -1 : 1 :));
}

mixed *query_player_listings(object player)
{
    mixed *all;
    mixed *out;
    mapping listing;
    string player_id;

    all = query_public_listings();
    out = ({ });
    player_id = ensure_player_id(player);
    foreach (listing in all) {
        if (!mapp(listing))
            continue;
        if (listing["seller_id"] == player_id || listing["current_bidder"] == player_id)
            out += ({ listing });
    }

    return out;
}

string render_public_listings(int page)
{
    mixed *listings;
    mapping listing;
    string output;
    int end;
    int page_size;
    int start;
    int total_pages;

    listings = query_public_listings();
    if (!arrayp(listings) || sizeof(listings) == 0)
        return "当前没有拍卖中的物品。\n";

    page_size = 5;
    total_pages = (sizeof(listings) + page_size - 1) / page_size;
    if (page <= 0)
        page = 1;
    if (page > total_pages)
        page = total_pages;
    start = (page - 1) * page_size;
    end = start + page_size - 1;
    if (end >= sizeof(listings))
        end = sizeof(listings) - 1;

    output = sprintf("拍卖列表（第 %d/%d 页）：\n", page, total_pages);
    foreach (listing in listings[start..end]) {
        output += sprintf("  - %s [%s] 底价:%s 当前:%s 状态:%s\n",
            listing["id"] || "",
            listing["item_name"] || "物品",
            MONEY_D->format_money(listing["opening_price"] || 0),
            MONEY_D->format_money(listing["current_bid"] || 0),
            listing["status"] || "active");
    }
    if (total_pages > 1)
        output += "用 auction list <页码> 翻页。\n";
    return output;
}

string render_player_listings(object player)
{
    mapping listing;
    mixed *listings;
    string output;

    listings = query_player_listings(player);
    if (!arrayp(listings) || sizeof(listings) == 0)
        return "你当前没有相关拍卖记录。\n";

    output = "你的拍卖记录：\n";
    foreach (listing in listings) {
        output += sprintf("  - %s [%s] 状态:%s 当前:%s\n",
            listing["id"] || "",
            listing["item_name"] || "物品",
            listing["status"] || "",
            MONEY_D->format_money(listing["current_bid"] || 0));
    }
    return output;
}

mapping create_listing(object seller, string item_name, int opening_price, int duration)
{
    int *bounds;
    int deposit;
    mapping attachment;
    mapping listing;
    mapping money_result;
    mapping refund_result;
    mapping attachment_delivery;
    int save_result;
    int refund_queued;
    object item;
    string listing_id;
    string seller_id;

    if (!auction_state_ready())
        return ([ "success": 0, "state": "auction_state_unavailable",
            "message": "拍卖账本当前不可用，请稍后重试。\n" ]);
    if (!ECONOMY_SETTLEMENT_D->query_auction_runtime_snapshot()["enabled"])
        return ([ "success": 0, "message": "拍卖系统当前未启用。\n" ]);
    if (!objectp(seller) || !stringp(item_name) || item_name == "" || opening_price <= 0)
        return ([ "success": 0, "message": "用法：auction sell <物品> <底价> [时长]\n" ]);

    item = ITEM_INVENTORY_D->query_transferable_item(seller, item_name);
    if (!objectp(item))
        return ([ "success": 0, "message": "这件物品当前无法上架拍卖。\n" ]);
    attachment = ITEM_INVENTORY_D->build_item_attachment_from_object(item);
    if (!mapp(attachment))
        return ([ "success": 0, "message": "这件物品当前无法上架拍卖。\n" ]);

    bounds = ECONOMY_SETTLEMENT_D->query_auction_duration_bounds();
    if (duration <= 0)
        duration = bounds[0];
    if (duration < bounds[0])
        duration = bounds[0];
    if (duration > bounds[1])
        duration = bounds[1];

    deposit = ECONOMY_SETTLEMENT_D->query_auction_deposit(opening_price);
    seller_id = ensure_player_id(seller);
    listing_id = generate_listing_id();
    if (deposit > 0) {
        money_result = ECONOMY_SETTLEMENT_D->settle_money_operation(
            seller, "auction", "listing_deposit", "deduct", deposit,
            "auction/" + listing_id + "/deposit");
        if (!mapp(money_result) || money_result["ok"] != 1)
            return ([ "success": 0,
                "pending_recovery": mapp(money_result)
                    && money_result["pending_recovery"] ? 1 : 0,
                "message": mapp(money_result)
                    && money_result["pending_recovery"]
                    ? "保证金操作正在恢复，请稍后使用相同请求重试。\n"
                    : "无法扣除拍卖保证金。\n" ]);
    }
    if (!ITEM_INVENTORY_D->consume_attachment_item_from_player(seller, attachment)) {
        if (deposit > 0) {
            refund_result = ECONOMY_SETTLEMENT_D->settle_money_operation(
                seller, "auction", "listing_deposit_refund", "add", deposit,
                "auction/" + listing_id + "/deposit/refund");
            if (!mapp(refund_result) || refund_result["ok"] != 1)
                refund_queued = queue_compensation_with_operation(seller,
                    "auction", "listing_deposit_refund", seller_id, listing_id,
                    "auction/" + listing_id + "/deposit/refund", deposit);
        }
        return ([ "success": 0,
            "pending_recovery": deposit > 0
                && (!mapp(refund_result) || refund_result["ok"] != 1) ? 1 : 0,
            "compensation_queued": refund_queued,
            "message": "无法从你的背包中扣除上架物品。\n" ]);
    }

    listing = ([
        "id": listing_id,
        "seller_id": seller_id,
        "seller_name": query_player_display_name(seller),
        "attachment": attachment,
        "item_name": attachment["item_name"] || item_name,
        "opening_price": opening_price,
        "current_bid": 0,
        "current_bidder": "",
        "current_bidder_name": "",
        "deposit": deposit,
        "created_at": time(),
        "close_at": time() + duration,
        "status": "active",
        "claimed": ([ ]),
        "delivery": ([ ]),
        "pending_rewards": ([ ]),
        "pending_refunds": ([ ]),
    ]);

    _listings[listing_id] = listing;
    schedule_listing_close(listing);
    save_result = save_auction_data();
    if (save_result == 1)
        return ([ "success": 1, "listing_id": listing_id, "deposit": deposit,
            "state": "committed" ]);
    if (save_result == 0) {
        map_delete(_listings, listing_id);
        attachment_delivery = deliver_reward_or_mail(seller_id,
            ([ "attachments": ({ attachment }) ]), "拍卖上架回滚",
            "拍卖条目未落盘，已退回上架物品。\n",
            listing_id + "/create/item_refund");
        if (deposit > 0) {
            refund_result = ECONOMY_SETTLEMENT_D->settle_money_operation(
                seller, "auction", "listing_deposit_refund", "add", deposit,
                "auction/" + listing_id + "/deposit/refund");
            if (!mapp(refund_result) || refund_result["ok"] != 1)
                refund_queued = queue_compensation_with_operation(seller,
                    "auction", "listing_deposit_refund", seller_id, listing_id,
                    "auction/" + listing_id + "/deposit/refund", deposit);
        }
        if (!mapp(attachment_delivery) || !attachment_delivery["ok"]
            || (deposit > 0 && (!mapp(refund_result)
                || refund_result["ok"] != 1)))
            save_result = -1;
    }
    return ([ "success": 0, "listing_id": listing_id,
        "deposit": deposit, "state": save_result == -1
            ? "unknown" : save_result == 2
                ? "persisted_receipt_pending"
                : query_auction_save_phase()["outcome"] || "unknown",
        "pending_recovery": save_result != 0 ? 1 : 0,
        "compensation_queued": refund_queued,
        "message": save_result == 2 ? "拍卖已写入，收据尚未完成，请稍后确认。\n"
            : save_result == 0
                ? "拍卖保存失败，物品与保证金已按未落盘结果处理。\n"
                : "拍卖保存结果未知，已保留恢复责任，请稍后确认。\n" ]);
}

int recover_pending_auction_deliveries();

mapping place_bid(object bidder, string listing_id, int amount)
{
    mapping listing;
    mapping previous_listing;
    mapping money_result;
    int save_result;
    int compensation_result;

    if (!auction_state_ready())
        return ([ "success": 0, "state": "auction_state_unavailable",
            "message": "拍卖账本当前不可用，请稍后重试。\n" ]);
    listing = ensure_listing(listing_id);
    if (!mapp(listing) || (listing["status"] || "") != "active")
        return ([ "success": 0, "message": "拍卖条目不存在或已结束。\n" ]);
    if (!objectp(bidder) || amount <= 0)
        return ([ "success": 0, "message": "请输入有效出价。\n" ]);
    if (ensure_player_id(bidder) == (listing["seller_id"] || ""))
        return ([ "success": 0, "message": "不能给自己拍卖的物品出价。\n" ]);
    if (amount <= (listing["current_bid"] > 0 ? listing["current_bid"] : (listing["opening_price"] || 0)))
        return ([ "success": 0, "message": "出价必须高于当前价格。\n" ]);
    previous_listing = copy(listing);
    money_result = ECONOMY_SETTLEMENT_D->settle_money_operation(
        bidder, "auction", "bid", "deduct", amount,
        "auction/" + listing_id + "/bid/"
        + ensure_player_id(bidder) + "/" + amount);
    if (!mapp(money_result) || money_result["ok"] != 1)
        return ([ "success": 0,
            "pending_recovery": mapp(money_result)
                && money_result["pending_recovery"] ? 1 : 0,
            "operation_id": "auction/" + listing_id + "/bid/"
                + ensure_player_id(bidder) + "/" + amount,
            "message": mapp(money_result)
                && money_result["pending_recovery"]
                ? "竞价资金正在恢复，请稍后使用相同出价重试。\n"
                : "扣除竞价失败。\n" ]);
    if (stringp(listing["current_bidder"]) && listing["current_bidder"] != "")
        record_pending_bid_refund(listing, listing["current_bidder"],
            listing["current_bid"] || 0);

    listing["current_bid"] = amount;
    listing["current_bidder"] = ensure_player_id(bidder);
    listing["current_bidder_name"] = query_player_display_name(bidder);
    save_result = save_auction_data();
    if (save_result == 1) {
        recover_pending_auction_deliveries();
        return ([ "success": 1, "listing_id": listing_id,
            "current_bid": amount, "state": "committed",
            "pending_recovery": sizeof(listing["pending_refunds"] || ([]))
                ? 1 : 0 ]);
    }
    if (save_result == 0) {
        _listings[listing_id] = previous_listing;
        compensation_result = queue_compensation_with_operation(
            bidder, "auction", "bid_refund", ensure_player_id(bidder),
            listing_id, "auction/" + listing_id + "/bid/"
                + ensure_player_id(bidder) + "/" + amount + "/refund", amount);
        if (compensation_result != 1) {
            record_pending_bid_refund(_listings[listing_id],
                ensure_player_id(bidder), amount);
            save_auction_data();
        }
    }
    return ([ "success": 0, "listing_id": listing_id,
        "current_bid": previous_listing["current_bid"] || 0,
        "state": save_result == 2 ? "persisted_receipt_pending"
            : query_auction_save_phase()["outcome"] || "unknown",
        "pending_recovery": save_result != 0 ? 1 : 0,
        "message": "出价已进入持久化恢复流程，请稍后确认。\n" ]);
}

mapping cancel_listing(object seller, string listing_id)
{
    mapping listing;
    mapping previous_listing;
    mapping previous_claimed;
    mapping previous_delivery;
    int save_result;

    if (!auction_state_ready())
        return ([ "success": 0, "state": "auction_state_unavailable",
            "message": "拍卖账本当前不可用，请稍后重试。\n" ]);
    listing = ensure_listing(listing_id);
    if (!mapp(listing) || (listing["status"] || "") != "active")
        return ([ "success": 0, "message": "拍卖条目不存在或已结束。\n" ]);
    if (!objectp(seller) || ensure_player_id(seller) != (listing["seller_id"] || ""))
        return ([ "success": 0, "message": "只有卖家才能取消该拍卖。\n" ]);
    if (stringp(listing["current_bidder"]) && listing["current_bidder"] != "")
        return ([ "success": 0, "message": "已有出价的拍卖不能取消。\n" ]);

    previous_listing = copy(listing);
    cancel_close_timer(listing);
    listing["status"] = "cancelled";
    listing["closed_at"] = time();
    listing["pending_rewards"] = ([
        "seller": build_attachment_rewards(listing["attachment"], listing["deposit"] || 0),
    ]);
    listing["claimed"] = ([ "seller": 0 ]);
    listing["delivery"] = ([ ]);

    save_result = save_auction_data();
    if (save_result == 0) {
        _listings[listing_id] = previous_listing;
        schedule_listing_close(previous_listing);
        return ([ "success": 0, "listing_id": listing_id,
            "state": "not_written",
            "message": "拍卖取消未落盘，未发放物品或保证金。\n" ]);
    }
    if (save_result != 1) {
        return ([ "success": 0, "listing_id": listing_id,
            "status": "cancelled", "state": classify_auction_save_result(save_result),
            "pending_recovery": save_result != 0 ? 1 : 0,
            "message": auction_save_message(
                classify_auction_save_result(save_result), "取消") ]);
    }

    previous_claimed = mapp(listing["claimed"])
        ? copy(listing["claimed"]) : ([]);
    previous_delivery = mapp(listing["delivery"])
        ? copy(listing["delivery"]) : ([]);
    attempt_listing_delivery(listing, "seller");
    save_result = save_auction_data();
    if (save_result == 1)
        return build_listing_close_result(listing, "cancelled");
    log_file("auction", sprintf("%s cancelled delivery state pending: listing=%s phase=%O\\n",
        ctime(time()), listing_id, query_auction_save_phase()));
    return ([ "success": 0, "listing_id": listing_id,
        "status": "cancelled", "state": classify_auction_save_result(save_result),
        "pending_recovery": 1,
        "message": auction_save_message(
            classify_auction_save_result(save_result), "领取") ]);
}

mapping claim_listing(object player, string listing_id)
{
    mapping listing;
    mapping previous_claimed;
    mapping previous_delivery;
    int save_result;
    string player_id;

    if (!auction_state_ready())
        return ([ "success": 0, "state": "auction_state_unavailable",
            "message": "拍卖账本当前不可用，请稍后重试。\n" ]);
    listing = ensure_listing(listing_id);
    if (!mapp(listing))
        return ([ "success": 0, "message": "找不到该拍卖条目。\n" ]);
    player_id = ensure_player_id(player);
    if (player_id == (listing["seller_id"] || "") && !listing["claimed"]["seller"]) {
        previous_claimed = mapp(listing["claimed"])
            ? copy(listing["claimed"]) : ([]);
        previous_delivery = mapp(listing["delivery"])
            ? copy(listing["delivery"]) : ([]);
        attempt_listing_delivery(listing, "seller");
        save_result = save_auction_data();
        if (save_result == 1)
            return build_listing_close_result(listing, listing["status"] || "closed");
        if (save_result == 0) {
            listing["claimed"] = previous_claimed;
            listing["delivery"] = previous_delivery;
        }
        return ([ "success": 0, "listing_id": listing_id,
            "state": classify_auction_save_result(save_result),
            "pending_recovery": 1,
            "message": auction_save_message(
                classify_auction_save_result(save_result), "领取") ]);
    }
    if (player_id == (listing["current_bidder"] || "") && !listing["claimed"]["buyer"]) {
        previous_claimed = mapp(listing["claimed"])
            ? copy(listing["claimed"]) : ([]);
        previous_delivery = mapp(listing["delivery"])
            ? copy(listing["delivery"]) : ([]);
        attempt_listing_delivery(listing, "buyer");
        save_result = save_auction_data();
        if (save_result == 1)
            return build_listing_close_result(listing, listing["status"] || "closed");
        if (save_result == 0) {
            listing["claimed"] = previous_claimed;
            listing["delivery"] = previous_delivery;
        }
        return ([ "success": 0, "listing_id": listing_id,
            "state": classify_auction_save_result(save_result),
            "pending_recovery": 1,
            "message": auction_save_message(
                classify_auction_save_result(save_result), "领取") ]);
    }

    return ([ "success": 0, "message": "当前没有可领取的拍卖结算。\n" ]);
}

#define DELIVERY_RECOVERY_INTERVAL 300
#define DELIVERY_RECOVERY_BATCH 20
nosave int _delivery_recovery_handle;

void schedule_delivery_recovery();
int recover_pending_auction_deliveries();
int query_pending_delivery_count();

void create()
{
    mapping listing;

    ::create();
    _close_handles = ([]);
    restore_auction_data();
    foreach (listing in values(_listings || ([]))) {
        if (mapp(listing) && (listing["status"] || "") == "active")
            schedule_listing_close(listing);
    }
    /* L-191：收盘投递失败会留在 listing 的 pending_rewards 里，旧实现
     * 没有任何消费者重试，拍卖结果会永久卡住。这里挂上有界恢复扫描。 */
    schedule_delivery_recovery();
}

/* L-191：待投递奖励是唯一恢复依据——扫描未领取的 pending_rewards，
 * 复用同一条 attempt_listing_delivery（不另发替代奖励）。每轮有界，
 * 避免一次性冲击；仍为 pending 的条目保留到后续轮次。 */
void schedule_delivery_recovery()
{
    if (_delivery_recovery_handle)
        remove_call_out(_delivery_recovery_handle);
    _delivery_recovery_handle = call_out(
        "recover_pending_auction_deliveries", DELIVERY_RECOVERY_INTERVAL);
}

int recover_pending_auction_deliveries()
{
    mapping listing;
    mapping pending;
    mapping pending_refunds;
    mapping refund;
    mapping money_result;
    string *ids;
    string *refund_ids;
    string role;
    string refund_id;
    string player_id;
    string operation_id;
    object player;
    int recovered;
    int scanned;
    int amount;
    int mail_result;
    int save_result;
    int listing_changed;
    int listing_recovered;
    mapping previous_claimed;
    mapping previous_delivery;
    mapping previous_pending_refunds;

    _delivery_recovery_handle = 0;
    if (!retry_auction_save_receipt()) {
        schedule_delivery_recovery();
        return 0;
    }
    ids = mapp(_listings) ? keys(_listings) : ({ });
    foreach (string listing_id in ids) {
        listing = _listings[listing_id];
        if (!mapp(listing))
            continue;
        pending = listing["pending_rewards"];
        pending_refunds = listing["pending_refunds"];
        if ((!mapp(pending) || sizeof(pending) == 0)
            && (!mapp(pending_refunds) || sizeof(pending_refunds) == 0))
            continue;
        if (scanned >= DELIVERY_RECOVERY_BATCH)
            break;
        scanned++;

        listing_changed = 0;
        listing_recovered = 0;
        previous_claimed = mapp(listing["claimed"])
            ? copy(listing["claimed"]) : ([]);
        previous_delivery = mapp(listing["delivery"])
            ? copy(listing["delivery"]) : ([]);
        previous_pending_refunds = mapp(listing["pending_refunds"])
            ? copy(listing["pending_refunds"]) : ([]);

        if (mapp(pending))
            foreach (role in ({ "seller", "buyer" })) {
                if (!mapp(pending[role]))
                    continue;
                if (mapp(listing["claimed"]) && listing["claimed"][role])
                    continue;
                attempt_listing_delivery(listing, role);
                if (mapp(listing["claimed"])
                    && listing["claimed"][role]) {
                    listing_changed = 1;
                    listing_recovered++;
                }
            }

        if (mapp(pending_refunds) && sizeof(pending_refunds) > 0) {
            refund_ids = keys(pending_refunds);
            foreach (refund_id in refund_ids) {
                refund = pending_refunds[refund_id];
                if (!mapp(refund) || !intp(refund["amount"])
                    || refund["amount"] <= 0
                    || !stringp(refund["player_id"])
                    || refund["player_id"] == "")
                    continue;
                amount = refund["amount"];
                player_id = refund["player_id"];
                operation_id = stringp(refund["operation_id"])
                    ? refund["operation_id"] : "";
                if (operation_id == "")
                    operation_id = "auction/" + (listing["id"] || "auction")
                        + "/bid/" + player_id + "/" + amount + "/refund";
                player = find_online_player_by_id(player_id);
                if (objectp(player)) {
                    money_result = ECONOMY_SETTLEMENT_D->settle_money_operation(
                        player, "auction", "bid_refund", "add", amount,
                        operation_id);
                    if (!mapp(money_result) || money_result["ok"] != 1)
                        continue;
                    map_delete(pending_refunds, refund_id);
                    listing_changed = 1;
                    listing_recovered++;
                    continue;
                }
                MAIL_D->set_mail_operation_id(operation_id);
                mail_result = MAIL_D->send_system_mail(player_id,
                    sprintf("拍卖退款：%s", listing["item_name"] || "拍卖物品"),
                    "你在拍卖中的上一笔出价已退还。", ({ }), amount);
                MAIL_D->clear_mail_operation_id();
                if (mail_result == 1) {
                    map_delete(pending_refunds, refund_id);
                    listing_changed = 1;
                    listing_recovered++;
                }
            }
        }

        /* Rewards and refunds share one listing candidate.  A delivery-only
         * scan must also persist claimed/delivery; the old early continue
         * skipped that save and could lose the recovery cursor. */
        if (!listing_changed)
            continue;
        save_result = save_auction_data();
        if (save_result != 1) {
            /* A not-written listing candidate did not publish its delivery
             * cursor. Restore the cursor so the next scan retries the same
             * idempotent reward/mail/money operations. For persisted or
             * unknown outcomes keep the candidate in memory and let the
             * owner receipt recovery decide; rewinding would discard the
             * only evidence that may already be durable. */
            if (save_result == 0) {
                listing["claimed"] = previous_claimed;
                listing["delivery"] = previous_delivery;
                listing["pending_refunds"] = previous_pending_refunds;
            }
            log_file("auction", sprintf(
                "%s delivery/refund cursor pending: listing=%s phase=%O\\n",
                ctime(time()), listing["id"] || "", query_auction_save_phase()));
            continue;
        }
        recovered += listing_recovered;
    }
    schedule_delivery_recovery();
    return recovered;
}

int query_pending_delivery_count()
{
    mapping listing;
    int count;

    if (!mapp(_listings))
        return 0;
    foreach (listing in values(_listings)) {
        if (!mapp(listing))
            continue;
        if (mapp(listing["pending_rewards"]))
            count += sizeof(keys(listing["pending_rewards"]));
        if (mapp(listing["pending_refunds"]))
            count += sizeof(keys(listing["pending_refunds"]));
    }
    return count;
}
