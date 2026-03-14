#include "device_attach_helpers.h"

#include "spd_protocol.h"
#include "../common.h"

extern int& m_bOpened;

bool is_device_unattached_and_log(spdio_t *io) {
    (void)io; // 当前仅依赖全局 m_bOpened，与 io 本身无关

    if (m_bOpened == -1) {
        DEG_LOG(E, "device unattached, exiting...");
        return true;
    }

    return false;
}
