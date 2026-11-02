/**
 * @file    name_d.c
 * @brief   名字服务



 */

#pragma strict_types

inherit "/lib/framework/daemon";

private nosave string *_surnames = ({
    "张", "王", "李", "赵", "刘", "陈", "杨", "黄", "周", "吴"
});

private nosave string *_male_names = ({
    "强", "勇", "伟", "杰", "峰", "磊", "涛", "军", "明", "浩"
});

private nosave string *_female_names = ({
    "芳", "娜", "敏", "静", "丽", "艳", "玲", "燕", "婷", "雪"
});

void create()
{
    ::create();
}

string generate_random_name(string gender)
{
    string surname;
    string given1;
    string given2;
    string *pool;

    surname = _surnames[random(sizeof(_surnames))];
    pool = (gender == "女性" || gender == "女") ? _female_names : _male_names;
    given1 = pool[random(sizeof(pool))];
    given2 = pool[random(sizeof(pool))];

    if (random(100) < 50) {
        return surname + given1;
    }

    return surname + given1 + given2;
}
