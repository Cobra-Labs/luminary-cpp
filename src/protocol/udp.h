//
// Created by janwin443 on 6/30/26.
//

#pragma once
#include <iostream>

namespace luminary::core {

    class UDPManager {
        public:
            static int send(const uint8_t* buffer, size_t size, const char* ipAddress, uint16_t port);
    };

}
