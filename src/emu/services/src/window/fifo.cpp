/*
 * Copyright (c) 2019 EKA2L1 Team
 *
 * This file is part of EKA2L1 project
 * (see bentokun.github.com/EKA2L1).
 *
 * Initial contributor: pent0
 * Contributors:
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

#include <common/log.h>
#include <services/window/fifo.h>

#include <cassert>

namespace eka2l1::epoc {
    static bool is_key_up_down_event(const epoc::event_code evt) {
        return (evt == epoc::event_code::key_down) || (evt == epoc::event_code::key_up);
    }

    static epoc::event_code matching_key_up_down_event(const epoc::event_code evt) {
        return (evt == epoc::event_code::key_down) ? epoc::event_code::key_up : epoc::event_code::key_down;
    }

    static bool is_same_key(const epoc::event &lhs, const epoc::event &rhs) {
        return (lhs.handle == rhs.handle) && (lhs.key_evt_.scancode == rhs.key_evt_.scancode)
            && (lhs.key_evt_.code == rhs.key_evt_.code);
    }

    static bool is_pointer_up_down_pair(const epoc::event_type lhs, const epoc::event_type rhs) {
        return ((lhs == epoc::event_type::button1down) && (rhs == epoc::event_type::button1up))
            || ((lhs == epoc::event_type::button1up) && (rhs == epoc::event_type::button1down))
            || ((lhs == epoc::event_type::button2down) && (rhs == epoc::event_type::button2up))
            || ((lhs == epoc::event_type::button2up) && (rhs == epoc::event_type::button2down))
            || ((lhs == epoc::event_type::button3down) && (rhs == epoc::event_type::button3up))
            || ((lhs == epoc::event_type::button3up) && (rhs == epoc::event_type::button3down));
    }

    static bool is_same_pointer(const epoc::event &lhs, const epoc::event &rhs) {
        return (lhs.handle == rhs.handle) && (lhs.adv_pointer_evt_.ptr_num == rhs.adv_pointer_evt_.ptr_num);
    }

    bool event_fifo::is_my_priority_really_high(epoc::event_code evt) {
        switch (evt) {
        case event_code::switch_off:
        case event_code::switch_on:
        case event_code::event_password:
            return true;

        default:
            return false;
        }
    }

    std::uint32_t event_fifo::queue_event(const event &evt) {
        const std::lock_guard<std::mutex> guard(lock_);

        if (q_.size() == maximum_element) {
            do_purge();
        }

        if ((evt.type == epoc::event_code::touch) && ((evt.adv_pointer_evt_.evtype == epoc::event_type::drag) || (evt.adv_pointer_evt_.evtype == epoc::event_type::move))) {
            if (!q_.empty()) {
                epoc::event &evt_last = q_.back().evt;

                // Same delivery
                if ((evt_last.handle == evt.handle) && (evt_last.type == evt.type) && (evt_last.adv_pointer_evt_.evtype == evt.adv_pointer_evt_.evtype)
                    && (evt_last.adv_pointer_evt_.ptr_num == evt.adv_pointer_evt_.ptr_num)) {
                    evt_last = evt;
                    return static_cast<std::uint32_t>(q_.size());
                    ;
                }
            }
        }

        std::uint32_t result = queue_event_dont_care(evt);
        trigger_notification();

        return result;
    }

    // Symbian purges:
    // Pointer up/down pairs
    // Key messages
    // Key updown pairs
    //
    // Focus lost/gain pair
    // Not purge
    // Lone pointer ups
    // Lone focus lost/gain
    void event_fifo::do_purge() {
        const std::size_t original_size = q_.size();

        for (size_t i = 0; i < q_.size();) {
            switch (q_[i].evt.type) {
            case epoc::event_code::event_password:
            case epoc::event_code::switch_off:
                break;

            case epoc::event_code::null:
            case epoc::event_code::key:
            case epoc::event_code::modifier_change:
            case epoc::event_code::touch_enter:
            case epoc::event_code::touch_exit: {
                q_.erase(q_.begin() + i);
                continue;
            }

            case epoc::event_code::key_down:
            case epoc::event_code::key_up: {
                const epoc::event &key_evt = q_[i].evt;
                const epoc::event_code matching_type = matching_key_up_down_event(key_evt.type);
                auto matching = std::find_if(q_.begin() + i + 1, q_.end(),
                    [&](const fifo_element &elem) {
                        return (elem.evt.type == matching_type) && is_same_key(key_evt, elem.evt);
                    });

                if (matching != q_.end()) {
                    q_.erase(matching);
                    q_.erase(q_.begin() + i);
                    continue;
                }

                break;
            }

            case epoc::event_code::touch: {
                const epoc::event &touch_evt = q_[i].evt;
                auto matching = std::find_if(q_.begin() + i + 1, q_.end(),
                    [&](const fifo_element &elem) {
                        return (elem.evt.type == epoc::event_code::touch)
                            && is_same_pointer(touch_evt, elem.evt)
                            && is_pointer_up_down_pair(touch_evt.adv_pointer_evt_.evtype, elem.evt.adv_pointer_evt_.evtype);
                    });

                if (matching != q_.end()) {
                    q_.erase(matching);
                    q_.erase(q_.begin() + i);
                    continue;
                }

                if ((touch_evt.adv_pointer_evt_.evtype == epoc::event_type::drag)
                    || (touch_evt.adv_pointer_evt_.evtype == epoc::event_type::move)) {
                    q_.erase(q_.begin() + i);
                    continue;
                }

                break;
            }

            case epoc::event_code::focus_gained:
            case epoc::event_code::focus_lost: {
                if ((i + 1 < q_.size()) && ((q_[i + 1].evt.type == epoc::event_code::focus_gained) || (q_[i + 1].evt.type == epoc::event_code::focus_lost))) {
                    q_.erase(q_.begin() + i + 1);
                    q_.erase(q_.begin() + i);
                    continue;
                }

                break;
            }

            case epoc::event_code::switch_on: {
                if (i + 1 < q_.size() && (q_[i + 1].evt.type == epoc::event_code::switch_on)) {
                    q_.erase(q_.begin() + i);
                    continue;
                }

                break;
            }

            default: {
                break;
            }
            }

            i++;
        }

        for (size_t i = 0; (q_.size() >= maximum_element) && (i < q_.size());) {
            if (is_key_up_down_event(q_[i].evt.type)) {
                q_.erase(q_.begin() + i);
                continue;
            }

            i++;
        }

        if (q_.size() != original_size) {
            LOG_TRACE(SERVICE_WINDOW, "Purged {} stale window events from FIFO", original_size - q_.size());
        }
    }

    event event_fifo::get_event() {
        std::optional<event> evt = get_evt_opt();

        if (!evt) {
            // Create a null event
            return event(0, event_code::null);
        }

        return *evt;
    }

    std::uint32_t redraw_fifo::queue_event(void *owner, const redraw_event &evt, const std::uint16_t pri) {
        const std::lock_guard<std::mutex> guard(lock_);
        eka2l1::rect target_queue_rect(evt.top_left, evt.bottom_right);
        target_queue_rect.transform_from_symbian_rectangle();

        std::size_t limit = q_.size();

        for (std::size_t i = 0; i < limit; i++) {
            eka2l1::rect queued_rect(q_[i].evt.evt_.top_left, q_[i].evt.evt_.bottom_right);
            queued_rect.transform_from_symbian_rectangle();

            if ((q_[i].evt.evt_.handle == evt.handle) && target_queue_rect.contains(queued_rect)) {
                // The new redraw rect contains the old queued redraw rect. Remove it to avoid
                // unneccessary redraws.
                q_.erase(q_.begin() + i);
                limit--;
            }
        }

        redraw_event_full full_event;
        full_event.owner_ = owner;
        full_event.evt_ = evt;

        std::uint32_t id = queue_event_dont_care(full_event);
        q_.back().pri = pri;

        std::stable_sort(q_.begin(), q_.end(),
            [&](const fifo_element &e1, const fifo_element &e2) {
                return e1.pri < e2.pri;
            });

        // Queue a redraw won't directly trigger a notification.
        return id;
    }

    void redraw_fifo::remove_events(void *owner) {
        const std::lock_guard<std::mutex> guard(lock_);
        common::erase_elements(q_, [owner](fifo_element &elem) -> bool {
            return elem.evt.owner_ == owner;
        });
    }
}
