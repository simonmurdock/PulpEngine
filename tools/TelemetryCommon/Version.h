#pragma once

#define __TELM_VERSION_PP_STRINGIZE_HELPER(token) #token
#define __TELM_VERSION_PP_STRINGIZE(str) __TELM_VERSION_PP_STRINGIZE_HELPER(str)


#define TELEM_NAME "Tom's Telemetry"
#define TELEM_VERSION_MAJOR 0
#define TELEM_VERSION_MINOR 1
#define TELEM_VERSION_PATCH 0
#define TELEM_VERSION_BUILD 0
#define TELEM_VERSION_STR \
    __TELM_VERSION_PP_STRINGIZE(TELEM_VERSION_MAJOR) "." \
    __TELM_VERSION_PP_STRINGIZE(TELEM_VERSION_MINOR) "." \
    __TELM_VERSION_PP_STRINGIZE(TELEM_VERSION_PATCH) "." \
    __TELM_VERSION_PP_STRINGIZE(TELEM_VERSION_BUILD)


#define TELEM_COPYRIGHT_STR       "Copyright Tom Crowley (C) 2019"
#define TELEM_TRADEMARK_STR       ""
#define TELEM_COMPANY_STR         ""
