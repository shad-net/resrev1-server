// SPDX-FileCopyrightText: Copyright 2026 Resident Evil Revelations Custom Server Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <optional>
#include <QByteArray>
#include <QByteArrayView>

// Blowfish in ECB mode with big-endian block words, matching the game's implementation.
class Blowfish {
public:
    static constexpr qsizetype BlockSize = 8;

    explicit Blowfish(QByteArrayView key);
    QByteArray encrypt(QByteArray plain, char padding) const;
    std::optional<QByteArray> decrypt(QByteArrayView data) const;

private:
    uint32_t f(uint32_t x) const;
    void encryptBlock(uint32_t& left, uint32_t& right) const;
    void decryptBlock(uint32_t& left, uint32_t& right) const;

    uint32_t p_[18];
    uint32_t s_[4][256];
};
