#ifndef __PURGE_VERSION_H
#define __PURGE_VERSION_H

#ifdef __cplusplus
extern "C" {
#endif

#define PURGE_FW_VERSION_MAJOR 1U
#define PURGE_FW_VERSION_MINOR 0U
#define PURGE_FW_VERSION_PATCH 0U

#define PURGE_FW_VERSION_BASE_STR "1.0.0"

#include "Purge_Version_Generated.h"

#define PURGE_FW_VERSION_STR PURGE_FW_VERSION_BASE_STR "+" PURGE_FW_GIT_SHA " " PURGE_FW_BUILD_TIME

#ifdef __cplusplus
}
#endif

#endif /* __PURGE_VERSION_H */
