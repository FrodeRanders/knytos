/* SPDX-License-Identifier: Apache-2.0 */
#include <limits.h>
#include "erts_calendar.h"

static void calendar_values(void)
{
    static const struct { Sint y, m, d, h, min, s; Sint64 seconds; } vectors[] = {
        {1600, 1, 1, 0, 0, 0, -11676096000L},
        {1900, 3, 1, 0, 0, 0, -2203891200L},
        {1969, 12, 31, 23, 59, 59, -1},
        {1970, 1, 1, 0, 0, 0, 0},
        {2000, 2, 29, 12, 34, 56, 951827696},
        {2100, 3, 1, 0, 0, 0, 4107542400L},
        {2262, 4, 11, 23, 47, 16, 9223372036L},
        {2400, 2, 29, 0, 0, 0, 13574563200L},
    };
    for (unsigned i = 0; i < sizeof(vectors)/sizeof(vectors[0]); ++i) {
        Sint y, m, d, h, min, s;
        Sint64 seconds;
        assert(univ_to_seconds(vectors[i].y, vectors[i].m, vectors[i].d,
                               vectors[i].h, vectors[i].min, vectors[i].s, &seconds));
        assert(seconds == vectors[i].seconds);
        assert(seconds_to_univ(seconds, &y, &m, &d, &h, &min, &s));
        assert(y == vectors[i].y && m == vectors[i].m && d == vectors[i].d);
        assert(h == vectors[i].h && min == vectors[i].min && s == vectors[i].s);
        for (int dst = -1; dst <= 1; ++dst) {
            assert(local_to_univ(&y, &m, &d, &h, &min, &s, dst));
            assert(univ_to_local(&y, &m, &d, &h, &min, &s));
            assert(y == vectors[i].y && m == vectors[i].m && d == vectors[i].d);
            assert(h == vectors[i].h && min == vectors[i].min && s == vectors[i].s);
        }
    }
    /* Independent calendar-day iteration covers every day in a full
     * Gregorian 400-year cycle, rather than only conversion round trips. */
    Sint y = 2000, m = 1, d = 1;
    static const int lengths[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    for (Sint64 day = 0; day < 146097; ++day) {
        Sint yy, mm, dd, h, min, s;
        Sint64 seconds = 946684800L + day * 86400;
        assert(seconds_to_univ(seconds, &yy, &mm, &dd, &h, &min, &s));
        assert(yy == y && mm == m && dd == d && !h && !min && !s);
        Sint64 back;
        assert(univ_to_seconds(y, m, d, 0, 0, 0, &back) && back == seconds);
        int length = lengths[m - 1] + (m == 2 && y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
        if (++d > length) { d = 1; if (++m > 12) { m = 1; ++y; } }
    }
    Sint h = 0, min = 0, s = 0;
    y = INT_MAX - 1; m = 12; d = 31;
    Sint64 seconds;
    assert(univ_to_seconds(y, m, d, h, min, s, &seconds));
    Sint yy, mm, dd;
    assert(seconds_to_univ(seconds, &yy, &mm, &dd, &h, &min, &s));
    assert(yy == y && mm == m && dd == d);
    static const Sint invalid[][6] = {
        {1599,1,1,0,0,0}, {INT_MAX,1,1,0,0,0}, {2000,0,1,0,0,0},
        {2000,13,1,0,0,0}, {1900,2,29,0,0,0}, {2100,2,29,0,0,0},
        {2000,1,0,0,0,0}, {2000,4,31,0,0,0}, {2000,1,1,24,0,0},
        {2000,1,1,0,60,0}, {2000,1,1,0,0,60},
    };
    for (unsigned i = 0; i < sizeof(invalid)/sizeof(invalid[0]); ++i) {
        y=invalid[i][0]; m=invalid[i][1]; d=invalid[i][2];
        h=invalid[i][3]; min=invalid[i][4]; s=invalid[i][5];
        seconds = -123;
        assert(!univ_to_seconds(y,m,d,h,min,s,&seconds) && seconds == -123);
        assert(!local_to_univ(&y,&m,&d,&h,&min,&s,1));
        assert(!univ_to_local(&y,&m,&d,&h,&min,&s));
        assert(y==invalid[i][0] && m==invalid[i][1] && d==invalid[i][2]);
        assert(h==invalid[i][3] && min==invalid[i][4] && s==invalid[i][5]);
    }
    puts("ERTS CALENDAR vectors full Gregorian cycle boundaries and UTC policy PASS");
}

static void calendar_current(void)
{
    for (int universal = 0; universal < 2; ++universal) {
        int y,m,d,h,min,s;
        Sint64 before = erts_os_system_time()/1000000000;
        if (universal) get_universaltime(&y,&m,&d,&h,&min,&s);
        else get_localtime(&y,&m,&d,&h,&min,&s);
        Sint64 after = erts_os_system_time()/1000000000, seconds;
        assert(univ_to_seconds(y,m,d,h,min,s,&seconds) && before <= seconds && seconds <= after);
    }
    int y,m,d,h,min,s;
    Sint64 before = erts_os_system_time()/1000000000;
    get_date(&y,&m,&d);
    Sint64 after = erts_os_system_time()/1000000000, date;
    assert(univ_to_seconds(y,m,d,0,0,0,&date));
    assert(before/86400 <= date/86400 && date/86400 <= after/86400);
    before = erts_os_system_time()/1000000000;
    get_time(&h,&min,&s);
    after = erts_os_system_time()/1000000000;
    assert(h>=0 && h<24 && min>=0 && min<60 && s>=0 && s<60);
    Sint64 seconds = h*3600 + min*60 + s;
    if (before/86400 == after/86400) assert(before%86400 <= seconds && seconds <= after%86400);
    else if (after-before < 86400) assert(before%86400 <= seconds || seconds <= after%86400);
    struct tm broken;
    assert(knytos_erts_calendar_now(&broken) == KNYTOS_OK && !broken.tm_isdst);
}
