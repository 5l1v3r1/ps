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

#include <QtWidgets>

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow();
    ~MainWindow();

    // Renders the VRAM data.
    void render_frame(const uint16_t* vram);

    // "File -> Insert CD-ROM image..."
    QAction* insert_cdrom_image;

    // "File -> Run PS-X EXE..."
    QAction* run_ps_exe;

    // "Debug -> Display libps log"
    QAction* display_libps_log;

    // "Emulation -> Start" or "Emulation -> Resume" depending on the run state
    // of the emulator
    QAction* start_emu;

    // "Emulation -> Stop"
    QAction* stop_emu;

    // "Emulation -> Pause"
    QAction* pause_emu;

    // "Emulation -> Reset"
    QAction* reset_emu;

private:
    // Image
    QImage* vram_image;

    // Image view
    QLabel* vram_image_view;

    QMenu* file_menu;
    QMenu* emulation_menu;
    QMenu* debug_menu;

    // Called when the user triggers "File -> Insert CD-ROM image..."
    void on_insert_cdrom_image();

    // Called when the user triggers "File -> Run PS-X EXE..."
    void on_run_ps_x_exe();

signals:
    // Called when the user triggers "File -> Insert CD-ROM image..."
    void selected_cdrom_image(const QString& file_name);

    // Emitted when the user selects a PS-X EXE.
    void selected_ps_x_exe(const QString& exe_file);
};