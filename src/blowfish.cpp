// SPDX-FileCopyrightText: Copyright 2026 Resident Evil Revelations Custom Server Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "blowfish.h"

#include "blowfish_tables.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace {

uint32_t loadBe(const char* p) {
    const auto* u = reinterpret_cast<const unsigned char*>(p);
    return (uint32_t(u[0]) << 24) | (uint32_t(u[1]) << 16) | (uint32_t(u[2]) << 8) | uint32_t(u[3]);
}

void storeBe(char* p, uint32_t v) {
    p[0] = char(v >> 24);
    p[1] = char(v >> 16);
    p[2] = char(v >> 8);
    p[3] = char(v);
}

} // namespace

Blowfish::Blowfish(QByteArrayView key) {
    std::memcpy(p_, BlowfishTables::P, sizeof(p_));
    std::memcpy(s_, BlowfishTables::S, sizeof(s_));

    const qsizetype len = key.size();
    qsizetype pos = 0;
    for (uint32_t& p : p_) {
        uint32_t word = 0;
        for (int i = 0; i < 4; ++i) {
            word = (word << 8) | static_cast<unsigned char>(len ? key[pos] : 0);
            pos = len ? (pos + 1) % len : 0;
        }
        p ^= word;
    }

    uint32_t left = 0;
    uint32_t right = 0;
    for (int i = 0; i < 18; i += 2) {
        encryptBlock(left, right);
        p_[i] = left;
        p_[i + 1] = right;
    }
    for (auto& box : s_) {
        for (int i = 0; i < 256; i += 2) {
            encryptBlock(left, right);
            box[i] = left;
            box[i + 1] = right;
        }
    }
}

uint32_t Blowfish::f(uint32_t x) const {
    return ((s_[0][x >> 24] + s_[1][(x >> 16) & 0xff]) ^ s_[2][(x >> 8) & 0xff]) + s_[3][x & 0xff];
}

void Blowfish::encryptBlock(uint32_t& left, uint32_t& right) const {
    for (int i = 0; i < 16; ++i) {
        left ^= p_[i];
        right ^= f(left);
        std::swap(left, right);
    }
    std::swap(left, right);
    right ^= p_[16];
    left ^= p_[17];
}

void Blowfish::decryptBlock(uint32_t& left, uint32_t& right) const {
    for (int i = 17; i > 1; --i) {
        left ^= p_[i];
        right ^= f(left);
        std::swap(left, right);
    }
    std::swap(left, right);
    right ^= p_[1];
    left ^= p_[0];
}

QByteArray Blowfish::encrypt(QByteArray plain, char padding) const {
    const qsizetype remainder = plain.size() % BlockSize;
    if (remainder != 0) {
        plain.append(BlockSize - remainder, padding);
    }
    char* data = plain.data();
    for (qsizetype off = 0; off < plain.size(); off += BlockSize) {
        uint32_t left = loadBe(data + off);
        uint32_t right = loadBe(data + off + 4);
        encryptBlock(left, right);
        storeBe(data + off, left);
        storeBe(data + off + 4, right);
    }
    return plain;
}

std::optional<QByteArray> Blowfish::decrypt(QByteArrayView data) const {
    if (data.size() % BlockSize != 0) {
        return std::nullopt;
    }
    QByteArray out(data.data(), data.size());
    char* buf = out.data();
    for (qsizetype off = 0; off < out.size(); off += BlockSize) {
        uint32_t left = loadBe(buf + off);
        uint32_t right = loadBe(buf + off + 4);
        decryptBlock(left, right);
        storeBe(buf + off, left);
        storeBe(buf + off + 4, right);
    }
    return out;
}
