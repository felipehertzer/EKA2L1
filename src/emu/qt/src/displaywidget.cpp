/*
 * Copyright (c) 2021 EKA2L1 Team.
 *
 * This file is part of EKA2L1 project.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <QGuiApplication>
#include <QKeyEvent>
#include <QMessageBox>
#include <QMetaObject>
#include <QMouseEvent>
#include <QThread>
#include <QWindow>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QEventPoint>
#endif

#include <common/log.h>
#include <qt/displaywidget.h>
#include <qt/utils.h>

#include <algorithm>
#include <utility>

#if !(defined(WIN32) || defined(__APPLE__) || defined(__HAIKU__))
#include <qpa/qplatformnativeinterface.h>
#endif

template <typename Func>
static void run_on_object_thread(QObject *object, Func &&func) {
    if (QThread::currentThread() == object->thread()) {
        func();
        return;
    }

    QMetaObject::invokeMethod(object, std::forward<Func>(func), Qt::BlockingQueuedConnection);
}

static eka2l1::drivers::window_system_type get_window_system_type() {
    // Determine WSI type based on Qt platform.
    QString platform_name = QGuiApplication::platformName();
    if (platform_name == QStringLiteral("windows"))
        return eka2l1::drivers::window_system_type::windows;
    else if (platform_name == QStringLiteral("cocoa"))
        return eka2l1::drivers::window_system_type::macOS;
    else if (platform_name == QStringLiteral("xcb"))
        return eka2l1::drivers::window_system_type::x11;
    else if (platform_name == QStringLiteral("wayland"))
        return eka2l1::drivers::window_system_type::wayland;
    else if (platform_name == QStringLiteral("haiku"))
        return eka2l1::drivers::window_system_type::haiku;

    QMessageBox::critical(nullptr, QStringLiteral("Error"), QString::asprintf("Unknown Qt platform: %s", platform_name.toStdString().c_str()));
    return eka2l1::drivers::window_system_type::headless;
}

static int normalized_key(const QKeyEvent *event) {
#if defined(__APPLE__)
    // macOS can report the top-row keys as system/media keys while still
    // carrying the hardware function-key virtual code.
    switch (event->nativeVirtualKey()) {
    case 122:
        return Qt::Key_F1;
    case 120:
        return Qt::Key_F2;
    case 99:
        return Qt::Key_F3;
    case 118:
        return Qt::Key_F4;
    case 96:
        return Qt::Key_F5;
    case 97:
        return Qt::Key_F6;
    case 98:
        return Qt::Key_F7;
    case 100:
        return Qt::Key_F8;
    case 101:
        return Qt::Key_F9;
    case 109:
        return Qt::Key_F10;
    case 103:
        return Qt::Key_F11;
    case 111:
        return Qt::Key_F12;
    default:
        break;
    }
#endif

    return event->key();
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
using qt_touch_point = QEventPoint;

static const QList<QEventPoint> &touch_points(QTouchEvent *event) {
    return event->points();
}

static QPointF touch_point_position(const QEventPoint &point) {
    return point.position();
}
#else
using qt_touch_point = QTouchEvent::TouchPoint;

static const QList<QTouchEvent::TouchPoint> &touch_points(QTouchEvent *event) {
    return event->touchPoints();
}

static QPointF touch_point_position(const QTouchEvent::TouchPoint &point) {
    return point.pos();
}
#endif

static QPointF mouse_event_position(const QMouseEvent *event) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->position();
#else
    return event->pos();
#endif
}

static eka2l1::vec3 scaled_touch_position(const qt_touch_point &point, const qreal pixel_ratio) {
    const QPointF pos = touch_point_position(point);

    return eka2l1::vec3(static_cast<int>(pos.x() * pixel_ratio),
        static_cast<int>(pos.y() * pixel_ratio),
        static_cast<int>(point.pressure() * eka2l1::PRESSURE_MAX_NUM));
}

static eka2l1::vec3 scaled_mouse_position(const QMouseEvent *event, const qreal pixel_ratio) {
    const QPointF pos = mouse_event_position(event);

    return eka2l1::vec3(static_cast<int>(pos.x() * pixel_ratio),
        static_cast<int>(pos.y() * pixel_ratio),
        0);
}

display_widget::display_widget(QWidget *parent)
    : QWidget(parent)
    , last_mouse_pos_{ 0.0, 0.0 }
    , held_mouse_buttons_(Qt::NoButton)
    , userdata_(nullptr) {
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_AcceptTouchEvents);
    setFocusPolicy(Qt::StrongFocus);

    setMouseTracking(false);

    windowHandle()->setSurfaceType(QWindow::VulkanSurface);
    windowHandle()->create();

    reset_active_pointers();
}

display_widget::~display_widget() {
}

eka2l1::drivers::window_system_info display_widget::get_window_system_info() {
    if (QThread::currentThread() != thread()) {
        eka2l1::drivers::window_system_info result;
        run_on_object_thread(this, [this, &result]() {
            result = get_window_system_info();
        });

        return result;
    }

    eka2l1::drivers::window_system_info wsi;
    wsi.type = get_window_system_type();

    QWindow *window = windowHandle();

// Our Win32 Qt external doesn't have the private API.
#if defined(WIN32) || defined(__APPLE__) || defined(__HAIKU__)
    wsi.render_window = window ? reinterpret_cast<void *>(window->winId()) : nullptr;
    wsi.render_surface = wsi.render_window;
#else
    QPlatformNativeInterface *pni = QGuiApplication::platformNativeInterface();
    wsi.display_connection = pni->nativeResourceForWindow("display", window);
    if (wsi.type == eka2l1::drivers::window_system_type::wayland)
        wsi.render_window = window ? pni->nativeResourceForWindow("surface", window) : nullptr;
    else
        wsi.render_window = window ? reinterpret_cast<void *>(window->winId()) : nullptr;

    wsi.render_surface = wsi.render_window;
#endif
    wsi.render_surface_scale = window ? static_cast<float>(window->devicePixelRatio()) : 1.0f;
    wsi.surface_width = window ? static_cast<std::uint32_t>(static_cast<qreal>(window->width()) * wsi.render_surface_scale) : 0;
    wsi.surface_height = window ? static_cast<std::uint32_t>(static_cast<qreal>(window->height()) * wsi.render_surface_scale) : 0;

    return wsi;
}

void display_widget::init(std::string title, eka2l1::vec2 size, const std::uint32_t flags) {
    if (QThread::currentThread() != thread()) {
        run_on_object_thread(this, [this, title = std::move(title), size, flags]() mutable {
            init(std::move(title), size, flags);
        });

        return;
    }

    resize(size.x, size.y);
    change_title(title);
    setFocus(Qt::OtherFocusReason);
}

void display_widget::resizeEvent(QResizeEvent *event) {
    if (resize_hook) {
        QWindow *window = windowHandle();

        if (window) {
            const qreal pixel_ratio = window->devicePixelRatio();
            resize_hook(userdata_, { static_cast<int>(event->size().width() * pixel_ratio), static_cast<int>(event->size().height() * pixel_ratio) });
        } else {
            resize_hook(userdata_, { event->size().width(), event->size().height() });
        }
    }
}

void display_widget::poll_events() {
    // Not to our business
}

void display_widget::shutdown() {
    // todo
}

void display_widget::set_fullscreen(const bool is_fullscreen) {
    if (QThread::currentThread() != thread()) {
        run_on_object_thread(this, [this, is_fullscreen]() {
            set_fullscreen(is_fullscreen);
        });

        return;
    }

    if (is_fullscreen) {
        showFullScreen();
    } else {
        showNormal();
    }
}

bool display_widget::should_quit() {
    return false;
}

void display_widget::change_title(std::string title) {
    if (QThread::currentThread() != thread()) {
        run_on_object_thread(this, [this, title = std::move(title)]() mutable {
            change_title(std::move(title));
        });

        return;
    }

    setWindowTitle(QString::fromUtf8(title.c_str()));
}

eka2l1::vec2 display_widget::window_size() {
    if (QThread::currentThread() != thread()) {
        eka2l1::vec2 result;
        run_on_object_thread(this, [this, &result]() {
            result = window_size();
        });

        return result;
    }

    return eka2l1::vec2(width(), height());
}

eka2l1::vec2 display_widget::window_fb_size() {
    if (QThread::currentThread() != thread()) {
        eka2l1::vec2 result;
        run_on_object_thread(this, [this, &result]() {
            result = window_fb_size();
        });

        return result;
    }

    const qreal pixel_ratio = devicePixelRatioF();
    return eka2l1::vec2(width() * pixel_ratio, height() * pixel_ratio);
}

eka2l1::vec2d display_widget::get_mouse_pos() {
    return last_mouse_pos_;
}

bool display_widget::get_mouse_button_hold(const int mouse_btt) {
    if ((mouse_btt < 0) || (mouse_btt > eka2l1::drivers::mouse_button_10)) {
        return false;
    }

    const int mouse_button_mask = 1 << mouse_btt;
    return (held_mouse_buttons_.toInt() & mouse_button_mask) != 0;
}

void display_widget::set_userdata(void *userdata) {
    userdata_ = userdata;
}

void *display_widget::get_userdata() {
    return userdata_;
}

bool display_widget::set_cursor(eka2l1::drivers::cursor *cur) {
    return true;
}

void display_widget::cursor_visiblity(const bool visi) {
}

bool display_widget::cursor_visiblity() {
    return true;
}

void display_widget::keyPressEvent(QKeyEvent *event) {
    if (event->isAutoRepeat()) {
        return;
    }

    if (button_pressed) {
        const int key = normalized_key(event);
        LOG_TRACE(eka2l1::FRONTEND_UI, "Qt key press: key=0x{:X}, native=0x{:X}, normalized=0x{:X}", event->key(), event->nativeVirtualKey(), key);
        button_pressed(userdata_, key);
        event->accept();
    }
}

void display_widget::keyReleaseEvent(QKeyEvent *event) {
    if (event->isAutoRepeat()) {
        return;
    }

    if (button_released) {
        const int key = normalized_key(event);
        LOG_TRACE(eka2l1::FRONTEND_UI, "Qt key release: key=0x{:X}, native=0x{:X}, normalized=0x{:X}", event->key(), event->nativeVirtualKey(), key);
        button_released(userdata_, key);
        event->accept();
    }
}

void display_widget::mousePressEvent(QMouseEvent *event) {
    const qreal pixel_ratio = devicePixelRatioF();
    const eka2l1::vec3 pos = scaled_mouse_position(event, pixel_ratio);
    last_mouse_pos_ = { static_cast<double>(pos.x), static_cast<double>(pos.y) };
    held_mouse_buttons_ = event->buttons();
    setFocus(Qt::MouseFocusReason);

    if (emit_raw_mouse_event(pos, qt_mouse_button_to_driver(event->button()), 0, 0)) {
        event->accept();
    }
}

void display_widget::mouseReleaseEvent(QMouseEvent *event) {
    const qreal pixel_ratio = devicePixelRatioF();
    const eka2l1::vec3 pos = scaled_mouse_position(event, pixel_ratio);
    last_mouse_pos_ = { static_cast<double>(pos.x), static_cast<double>(pos.y) };
    held_mouse_buttons_ = event->buttons();

    if (emit_raw_mouse_event(pos, qt_mouse_button_to_driver(event->button()), 2, 0)) {
        event->accept();
    }
}

void display_widget::mouseMoveEvent(QMouseEvent *event) {
    // Even with mouse tracking disabled, they still keep coming
    const qreal pixel_ratio = devicePixelRatioF();
    const eka2l1::vec3 pos = scaled_mouse_position(event, pixel_ratio);
    last_mouse_pos_ = { static_cast<double>(pos.x), static_cast<double>(pos.y) };
    held_mouse_buttons_ = event->buttons();

    if (emit_raw_mouse_event(pos, qt_mouse_button_to_driver(event->buttons()), 1, 0)) {
        event->accept();
    }
}

void display_widget::reset_active_pointers() {
    std::fill(active_pointers_.begin(), active_pointers_.end(), 0);
    std::fill(active_pointer_positions_.begin(), active_pointer_positions_.end(), eka2l1::vec3(0, 0, 0));
}

bool display_widget::emit_raw_mouse_event(eka2l1::vec3 pos, int button, int action, int mouse_id) {
    if (!raw_mouse_event) {
        return false;
    }

    raw_mouse_event(userdata_, pos, button, action, mouse_id);
    return true;
}

bool display_widget::event(QEvent *event) {
    switch (event->type()) {
    case QEvent::TouchBegin: {
        QTouchEvent *touch_event = static_cast<QTouchEvent *>(event);
        const auto &points = touch_points(touch_event);

        const qreal pixel_ratio = devicePixelRatioF();

        for (const qt_touch_point &point : points) {
            const auto free_pointer = std::find(active_pointers_.begin(), active_pointers_.end(), 0);
            if (free_pointer == active_pointers_.end()) {
                break;
            }

            const std::size_t pointer_index = static_cast<std::size_t>(std::distance(active_pointers_.begin(), free_pointer));
            active_pointers_[pointer_index] = point.id() + 1;
            active_pointer_positions_[pointer_index] = scaled_touch_position(point, pixel_ratio);
            emit_raw_mouse_event(active_pointer_positions_[pointer_index], eka2l1::drivers::mouse_button_left, 0,
                static_cast<int>(pointer_index));
        }

        event->accept();
        return true;
    }

    case QEvent::TouchUpdate: {
        QTouchEvent *touch_event = static_cast<QTouchEvent *>(event);
        const auto &points = touch_points(touch_event);

        const qreal pixel_ratio = devicePixelRatioF();

        for (const qt_touch_point &point : points) {
            bool found = false;
            for (std::size_t j = 0; j < active_pointers_.size(); j++) {
                if ((point.id() + 1) == active_pointers_[j]) {
                    active_pointer_positions_[j] = scaled_touch_position(point, pixel_ratio);
                    emit_raw_mouse_event(active_pointer_positions_[j], eka2l1::drivers::mouse_button_left, 1,
                        static_cast<int>(j));

                    found = true;
                    break;
                }
            }

            if (!found) {
                // Start a new one somewhere
                for (std::size_t j = 0; j < active_pointers_.size(); j++) {
                    if (active_pointers_[j] == 0) {
                        active_pointers_[j] = point.id() + 1;
                        active_pointer_positions_[j] = scaled_touch_position(point, pixel_ratio);
                        emit_raw_mouse_event(active_pointer_positions_[j], eka2l1::drivers::mouse_button_left, 0,
                            static_cast<int>(j));
                        break;
                    }
                }
            }
        }

        for (std::size_t i = 0; i < active_pointers_.size(); i++) {
            bool found = false;
            for (const qt_touch_point &point : points) {
                if (active_pointers_[i] == (point.id() + 1)) {
                    found = true;
                    break;
                }
            }

            if (active_pointers_[i] != 0 && !found) {
                emit_raw_mouse_event(active_pointer_positions_[i], eka2l1::drivers::mouse_button_left, 2, static_cast<int>(i));
                active_pointers_[i] = 0;
                active_pointer_positions_[i] = eka2l1::vec3(0, 0, 0);
            }
        }

        event->accept();
        return true;
    }

    case QEvent::TouchEnd:
    case QEvent::TouchCancel: {
        QTouchEvent *touch_event = static_cast<QTouchEvent *>(event);
        const auto &points = touch_points(touch_event);
        const qreal pixel_ratio = devicePixelRatioF();

        for (const qt_touch_point &point : points) {
            for (std::size_t i = 0; i < active_pointers_.size(); i++) {
                if (active_pointers_[i] == (point.id() + 1)) {
                    active_pointer_positions_[i] = scaled_touch_position(point, pixel_ratio);
                    break;
                }
            }
        }

        for (std::size_t i = 0; i < active_pointers_.size(); i++) {
            if (active_pointers_[i] != 0) {
                emit_raw_mouse_event(active_pointer_positions_[i], eka2l1::drivers::mouse_button_left, 2, static_cast<int>(i));
                active_pointers_[i] = 0;
                active_pointer_positions_[i] = eka2l1::vec3(0, 0, 0);
            }
        }

        event->accept();
        return true;
    }

    default:
        break;
    }

    return QWidget::event(event);
}
