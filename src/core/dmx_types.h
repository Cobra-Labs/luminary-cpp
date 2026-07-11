//
// Created by janwin443 on 6/27/26.
//
#pragma once
#include <string>

struct DmxAddress {
    int value; // 1-512

    explicit DmxAddress(int v) {
        if (v < 1 || v > 512)
            throw std::out_of_range("DMX address out of range: " + std::to_string(v));
        value = v;
    }
};
