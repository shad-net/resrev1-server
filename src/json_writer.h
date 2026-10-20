// SPDX-FileCopyrightText: Copyright 2026 Resident Evil Revelations Custom Server Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

class JsonWriter {
public:
    JsonWriter& beginObject() {
        return open('{');
    }
    JsonWriter& endObject() {
        return close('}');
    }
    JsonWriter& beginArray() {
        return open('[');
    }
    JsonWriter& endArray() {
        return close(']');
    }

    JsonWriter& key(const QString& name) {
        separate();
        appendString(name);
        out_ += ':';
        pendingValue_ = true;
        return *this;
    }

    JsonWriter& value(qint64 v) {
        separate();
        out_ += QByteArray::number(v);
        return *this;
    }

    JsonWriter& value(bool v) {
        separate();
        out_ += v ? "true" : "false";
        return *this;
    }

    JsonWriter& value(const QString& v) {
        separate();
        appendString(v);
        return *this;
    }

    JsonWriter& value(const char* v) {
        return value(QString::fromUtf8(v));
    }

    const QByteArray& data() const {
        return out_;
    }

private:
    JsonWriter& open(char c) {
        separate();
        out_ += c;
        first_.push_back(true);
        return *this;
    }

    JsonWriter& close(char c) {
        out_ += c;
        first_.pop_back();
        return *this;
    }

    void separate() {
        if (pendingValue_) {
            pendingValue_ = false;
            return;
        }
        if (!first_.isEmpty()) {
            if (!first_.back()) {
                out_ += ',';
            }
            first_.back() = false;
        }
    }

    void appendString(const QString& s) {
        out_ += '"';
        for (const QChar ch : s) {
            const char16_t c = ch.unicode();
            switch (c) {
            case '"':
                out_ += "\\\"";
                break;
            case '\\':
                out_ += "\\\\";
                break;
            case '\n':
                out_ += "\\n";
                break;
            case '\r':
                out_ += "\\r";
                break;
            case '\t':
                out_ += "\\t";
                break;
            default:
                if (c < 0x20 || c > 0x7e) {
                    out_ += "\\u" + QByteArray::number(c, 16).rightJustified(4, '0');
                } else {
                    out_ += char(c);
                }
            }
        }
        out_ += '"';
    }

    QByteArray out_;
    QVector<bool> first_;
    bool pendingValue_ = false;
};
