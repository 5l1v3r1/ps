// Copyright 2020 Michael Rodriguez
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
// SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
// OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
// CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

#pragma once

#include <array>
#include <QtWidgets>

class BIOSCalls : public QMainWindow
{
    Q_OBJECT

public:
    BIOSCalls();
    ~BIOSCalls();

    void add(const uint32_t pc, const uint32_t fn);

private:
    const std::array<std::string, 0x9F> a0_table =
    {
        "FileOpen",     // 0x00
        "FileSeek", // 0x01
        "FileRead", // 0x02
        "FileWrite",
        "FileClose",
        "FileIoctl",
        "exit",
        "FileGetDeviceFlag",
        "FileGetc",
        "FilePutc",
        "todigit",
        "atof",
    };

    const std::array<std::string, 0x9F> b0_table =
    {
        "FileOpen(%s, %s)",     // 0x00
        "FileSeek(%d, %d, %d)", // 0x01
        "FileRead(%d, %d, %d)", // 0x02
        "FileWrite",
        "FileClose",
        "FileIoctl",
        "exit(%d)",
        "FileGetDeviceFlag",
        "FileGetc",
        "FilePutc",
        "todigit('%c')",
        "atof(%s)",
    };

    const std::array<std::string, 0x9F> c0_table =
    {
        "FileOpen(%s, %s)",     // 0x00
        "FileSeek(%d, %d, %d)", // 0x01
        "FileRead(%d, %d, %d)", // 0x02
        "FileWrite",
        "FileClose",
        "FileIoctl",
        "exit(%d)",
        "FileGetDeviceFlag",
        "FileGetc",
        "FilePutc",
        "todigit('%c')",
        "atof(%s)",
    };

    unsigned int threshold;

    QWidget* widget;

    std::vector<QTreeWidgetItem*> call_list;
    QSpinBox* threshold_specifier;
    QTreeWidget* calls;

    QFormLayout* threshold_layout;
    QHBoxLayout* widget_layout;
};