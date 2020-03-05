#pragma once

namespace _ShortFilePrivate {
    using cstr = const char * const;

    static constexpr cstr past_last_slash(cstr str, cstr last_slash)
    {
    return
        *str == '\0' ? last_slash :
        *str == '/'  ? past_last_slash(str + 1, str + 1) :
        *str == '\\'  ? past_last_slash(str + 1, str + 1) :
        past_last_slash(str + 1, last_slash);
    }

    static constexpr cstr past_last_slash(cstr str)
    {
        return past_last_slash(str, str);
    }

}


/** Use this macro to get the filename of the current file */
#define __SHORT_FILE__ ({constexpr const char* const sf__ {_ShortFilePrivate::past_last_slash(__FILE__)}; sf__;})