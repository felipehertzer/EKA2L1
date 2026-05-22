/*
 * Copyright (c) 2018 EKA2L1 Team.
 *
 * This file is part of EKA2L1 project
 * (see bentokun.github.com/EKA2L1).
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

#include <algorithm>

#include <common/algorithm.h>
#include <common/configure.h>
#include <common/log.h>
#include <common/time.h>

#include <cstdlib>
#include <functional>
#include <kernel/kernel.h>
#include <kernel/scheduler.h>
#include <kernel/thread.h>
#include <kernel/timing.h>
#include <mem/mem.h>
#include <mem/mmu.h>
#include <mem/process.h>

namespace eka2l1::kernel {
    namespace {
        bool scheduler_diag_enabled() {
            static const bool enabled = (std::getenv("EKA2L1_FPS_DIAG") != nullptr)
                || (std::getenv("EKA2L1_SCHED_DIAG") != nullptr);
            return enabled;
        }

        const char *thread_state_name(const thread_state state) {
            switch (state) {
            case thread_state::create:
                return "create";
            case thread_state::run:
                return "run";
            case thread_state::wait:
                return "wait";
            case thread_state::ready:
                return "ready";
            case thread_state::stop:
                return "stop";
            case thread_state::wait_fast_sema:
                return "wait_fast_sema";
            case thread_state::wait_mutex:
                return "wait_mutex";
            case thread_state::wait_condvar:
                return "wait_condvar";
            case thread_state::wait_mutex_suspend:
                return "wait_mutex_suspend";
            case thread_state::wait_fast_sema_suspend:
                return "wait_fast_sema_suspend";
            case thread_state::wait_condvar_suspend:
                return "wait_condvar_suspend";
            case thread_state::hold_mutex_pending:
                return "hold_mutex_pending";
            case thread_state::wait_dfc:
                return "wait_dfc";
            case thread_state::wait_hle:
                return "wait_hle";
            default:
                return "unknown";
            }
        }

        const char *object_type_name(const object_type type) {
            switch (type) {
            case object_type::thread:
                return "thread";
            case object_type::process:
                return "process";
            case object_type::chunk:
                return "chunk";
            case object_type::library:
                return "library";
            case object_type::sema:
                return "sema";
            case object_type::mutex:
                return "mutex";
            case object_type::timer:
                return "timer";
            case object_type::server:
                return "server";
            case object_type::session:
                return "session";
            case object_type::logical_device:
                return "logical_device";
            case object_type::physical_device:
                return "physical_device";
            case object_type::logical_channel:
                return "logical_channel";
            case object_type::change_notifier:
                return "change_notifier";
            case object_type::undertaker:
                return "undertaker";
            case object_type::msg_queue:
                return "msg_queue";
            case object_type::prop_ref:
                return "prop_ref";
            case object_type::condvar:
                return "condvar";
            case object_type::codeseg:
                return "codeseg";
            case object_type::prop:
                return "prop";
            default:
                return "unknown";
            }
        }

        void report_scheduler_idle_diag(kernel_system *kern) {
            if (!scheduler_diag_enabled() || !kern) {
                return;
            }

            static std::uint64_t last_report_wall_us = 0;
            static std::uint64_t idle_entries = 0;

            idle_entries++;
            const std::uint64_t wall_now = common::get_current_utc_time_in_microseconds_since_epoch();
            if (last_report_wall_us != 0 && (wall_now - last_report_wall_us < common::microsecs_per_sec)) {
                return;
            }

            const std::uint64_t elapsed_ms = last_report_wall_us ? ((wall_now - last_report_wall_us) / 1000) : 0;
            last_report_wall_us = wall_now;

            int create_count = 0;
            int run_count = 0;
            int ready_count = 0;
            int wait_count = 0;
            int request_wait_count = 0;
            int sema_wait_count = 0;
            int mutex_wait_count = 0;
            int condvar_wait_count = 0;
            int hle_wait_count = 0;
            int stopped_count = 0;
            std::string waiters;

            for (const kernel_obj_unq_ptr &obj : kern->get_thread_list()) {
                thread *thr = reinterpret_cast<thread *>(obj.get());
                if (!thr) {
                    continue;
                }

                const thread_state state = thr->current_state();
                switch (state) {
                case thread_state::create:
                    create_count++;
                    break;
                case thread_state::run:
                    run_count++;
                    break;
                case thread_state::ready:
                case thread_state::hold_mutex_pending:
                    ready_count++;
                    break;
                case thread_state::wait_fast_sema:
                case thread_state::wait_fast_sema_suspend:
                    wait_count++;
                    sema_wait_count++;
                    break;
                case thread_state::wait_mutex:
                case thread_state::wait_mutex_suspend:
                    wait_count++;
                    mutex_wait_count++;
                    break;
                case thread_state::wait_condvar:
                case thread_state::wait_condvar_suspend:
                    wait_count++;
                    condvar_wait_count++;
                    break;
                case thread_state::wait_hle:
                    wait_count++;
                    hle_wait_count++;
                    break;
                case thread_state::wait:
                case thread_state::wait_dfc:
                    wait_count++;
                    break;
                case thread_state::stop:
                    stopped_count++;
                    break;
                }

                if (thr->wait_obj && thr->wait_obj->get_object_type() == object_type::sema
                    && thr->wait_obj->raw_name().find("requestSema") == 0) {
                    request_wait_count++;
                }

                if (state == thread_state::stop || state == thread_state::create || state == thread_state::ready) {
                    continue;
                }

                if (waiters.size() > 900) {
                    continue;
                }

                kernel::process *owner = thr->owning_process();
                waiters += waiters.empty() ? "" : "; ";
                waiters += owner ? owner->name() : "<no-process>";
                waiters += "/";
                waiters += thr->name();
                waiters += ":";
                waiters += thread_state_name(state);

                if (thr->wait_obj) {
                    waiters += " on ";
                    waiters += object_type_name(thr->wait_obj->get_object_type());
                    waiters += "(";
                    waiters += thr->wait_obj->raw_name();
                    waiters += ")";
                }
            }

            LOG_WARN(KERNEL,
                "Scheduler idle diag elapsed_ms={} idle_entries={} threads(create/run/ready/wait/stop)={}/{}/{}/{}/{} waits(request/sema/mutex/condvar/hle)={}/{}/{}/{}/{} [{}]",
                elapsed_ms,
                idle_entries,
                create_count,
                run_count,
                ready_count,
                wait_count,
                stopped_count,
                request_wait_count,
                sema_wait_count,
                mutex_wait_count,
                condvar_wait_count,
                hle_wait_count,
                waiters);
            idle_entries = 0;
        }
    }

    thread_scheduler::thread_scheduler(kernel_system *kern, ntimer *timing, arm::core *cpu)
        : kern(kern)
        , timing(timing)
        , run_core(cpu)
        , core_mmu(nullptr)
        , crr_thread(nullptr)
        , crr_process(nullptr) {
        wakeup_evt = timing->get_register_event("SchedulerWakeUpThread");

        if (wakeup_evt == -1) {
            wakeup_evt = timing->register_event("SchedulerWakeUpThread", [kern](std::uint64_t userdata, std::uint64_t cycles_late) {
                kernel::thread *thr = kern->get_by_id<kernel::thread>(static_cast<kernel::uid>(userdata));

                if (thr == nullptr) {
                    return;
                }

                thr->notify_sleep(0);
            });
        }

        // !!!
        std::fill(readys, readys + sizeof(readys) / sizeof(readys[0]), nullptr);
        idle_event.reset();
    }

    thread_scheduler::~thread_scheduler() {
        stop_idling();
    }

    void thread_scheduler::stop_idling() {
        if (kern->should_core_idle_when_inactive()) {
            idle_event.set();
        }
    }

    void thread_scheduler::switch_context(kernel::thread *oldt, kernel::thread *newt) {
        if (oldt) {
            oldt->real_time_active_end();
            run_core->save_context(oldt->ctx);

            if (oldt->state == thread_state::run) {
                oldt->state = thread_state::ready;
                oldt->time = oldt->timeslice;
            }

            oldt->decrease_access_count();
        }

        memory_system *mem = kern->get_memory_system();

        if (!core_mmu) {
            core_mmu = mem->get_mmu(run_core);
        }

        if (newt) {
            // cancel wake up
            // timing->unschedule_event(wakeup_evt, newt->unique_id());
            crr_thread = newt;
            crr_thread->state = thread_state::run;
            crr_thread->increase_access_count();
            crr_thread->real_time_active_begin();

            mem::mem_model_process *mm_process = crr_process ? crr_process->get_mem_model() : nullptr;

            if (crr_process != newt->owning_process()) {
                // Call the callbacks and release the process, no longerneed to read it
                kern->call_process_switch_callbacks(run_core, crr_process, newt->owning_process());

                if (crr_process)
                    crr_process->decrease_access_count();

                // Reference the use of the new process here
                crr_process = newt->owning_process();
                crr_process->increase_access_count();

                mm_process = crr_process->get_mem_model();

                core_mmu->set_current_addr_space(mm_process->address_space_id());

                run_core->flush_tlb();

                // NOTE: This is not needed now
                // run_core->set_asid(mm_process->address_space_id());
            }

            run_core->load_context(crr_thread->ctx);
            // LOG_TRACE(KERNEL, "Switched to {}", crr_thread->name());
        } else {
            // No current thread is eligible to run. Let the core that this scheduler currently handle sleeps.
            crr_thread = nullptr;
            report_scheduler_idle_diag(kern);

            // Let free access to kernel now
            if (kern->should_core_idle_when_inactive()) {
                kern->unlock();
                idle_event.wait();
                idle_event.reset();
                kern->lock();
            }
        }
    }

    kernel::thread *thread_scheduler::next_ready_thread() {
        if (ready_mask[0] != 0) {
            // Check the most significant bit and get the non-empty read queue
            int non_empty = common::find_most_significant_bit_one(ready_mask[0]);

            if (non_empty > 0) {
                return readys[non_empty - 1];
            }
        }

        if (ready_mask[1] == 0) {
            return nullptr;
        }

        const int non_empty = common::find_most_significant_bit_one(ready_mask[1]);

        if (non_empty > 0) {
            return readys[non_empty + 31];
        }

        return nullptr;
    }

    void thread_scheduler::reschedule() {
        kernel::thread *crr_thread = current_thread();
        kernel::thread *next_thread = next_ready_thread();

        if (next_thread && next_thread->time == 0) {
            // Restart the time
            kernel::thread *old_friend = next_thread;
            next_thread->time = next_thread->timeslice;

            if (next_thread->scheduler_link.next != next_thread || next_thread->scheduler_link.previous != next_thread) {
                // Move it to the end, and get the new thread next to it.
                readys[next_thread->real_priority] = next_thread->scheduler_link.next;
                next_thread = next_thread->scheduler_link.next;
            } else {
                // Deque the thread from ready queue in order to get the next highest priority and ready thread
                dequeue_thread_from_ready(next_thread);

                next_thread = next_ready_thread();
                queue_thread_ready(old_friend);
            }

            if (!next_thread && kern->should_core_idle_when_inactive()) {
                // Use our old outdated friend, it seems only one thread exists
                next_thread = old_friend;
            }
        }

        switch_context(crr_thread, next_thread);
    }

    void thread_scheduler::queue_thread_ready(kernel::thread *thr) {
        // If the ready queue at the target's thread priority is empty, add it
        if (readys[thr->real_priority] == nullptr) {
            readys[thr->real_priority] = thr;
            ready_mask[thr->real_priority >> 5] |= (1 << (thr->real_priority & 31));

            thr->scheduler_link.next = thr;
            thr->scheduler_link.previous = thr;

            // Well no need to idle anymore :D
            if (kern->should_core_idle_when_inactive() && !crr_thread)
                idle_event.set();

            return;
        }

        // Add it to the end.
        // The first thread in the queue has previous link linked to the last element
        thr->scheduler_link.previous = readys[thr->real_priority]->scheduler_link.previous;

        // Since our target thread is the last in the ready queue, the next pointer of our target thread
        // should points to the beginning of the ready queue
        thr->scheduler_link.next = readys[thr->real_priority];

        thr->scheduler_link.previous->scheduler_link.next = thr;
        readys[thr->real_priority]->scheduler_link.previous = thr;

        // Well no need to idle anymore :D
        if (kern->should_core_idle_when_inactive() && !crr_thread)
            idle_event.set();
    }

    void thread_scheduler::dequeue_thread_from_ready(kernel::thread *thr) {
        if (!(ready_mask[thr->real_priority >> 5] & (1 << (thr->real_priority & 31)))) {
            // The ready queue for this priority is empty. So what the hell
            return;
        }

        if (thr->scheduler_link.next == thr && thr->scheduler_link.previous == thr) {
            // Only one thread left for the queue. Empty the queue
            thr->scheduler_link.next = nullptr;
            thr->scheduler_link.previous = nullptr;

            readys[thr->real_priority] = nullptr;
            ready_mask[thr->real_priority >> 5] &= ~(1 << (thr->real_priority & 31));

            return;
        }

        // Dequeue
        thr->scheduler_link.next->scheduler_link.previous = thr->scheduler_link.previous;
        thr->scheduler_link.previous->scheduler_link.next = thr->scheduler_link.next;

        if (thr == readys[thr->real_priority]) {
            // The ready queue at the priority has the target thread as first element, before
            // it being removed. So let's set the first element to next robin-rounded thread
            // of the target thread
            readys[thr->real_priority] = thr->scheduler_link.next;
        }

        // Empty the link
        thr->scheduler_link.next = nullptr;
        thr->scheduler_link.previous = nullptr;
    }

    // Put the thread into the ready queue to run in the next core timing yeid
    bool thread_scheduler::schedule(kernel::thread *thr) {
        if (thr->state == thread_state::run || thr->state == thread_state::ready || thr->state == thread_state::hold_mutex_pending) {
            return false;
        }

        thr->state = thread_state::ready;

        queue_thread_ready(thr);
        kern->prepare_reschedule();

        return true;
    }

    bool thread_scheduler::sleep(kernel::thread *thr, uint32_t sl_time, const bool deque) {
        if (crr_thread != thr) {
            return false;
        }

        if (deque) {
            // It's already waiting
            if (thr->state == thread_state::wait || thr->state == thread_state::ready) {
                return false;
            }

            thr->state = thread_state::wait;
            dequeue_thread_from_ready(thr);
        }

        // Schedule the thread to be waken up
        timing->schedule_event(static_cast<std::uint64_t>(sl_time), wakeup_evt, thr->unique_id());

        return true;
    }

    void thread_scheduler::unschedule_wakeup() {
        timing->unschedule_event(wakeup_evt, crr_thread->unique_id());
    }

    bool thread_scheduler::wait(kernel::thread *thr) {
        // It's already waiting
        if (thr->state == thread_state::wait || thr->state == thread_state::ready
            || thr->state == thread_state::wait_fast_sema || thr->state == thread_state::wait_mutex) {
            return false;
        }

        dequeue_thread_from_ready(thr);
        kern->prepare_reschedule();

        return true;
    }

    bool thread_scheduler::dewait(kernel::thread *thr) {
        if (thr->scheduler_link.next != nullptr || thr->scheduler_link.previous != nullptr) {
            return false;
        }

        switch (thr->state) {
        case thread_state::ready:
        case thread_state::run:
        case thread_state::stop:
            return false;

        default:
            break;
        }

        thr->state = thread_state::ready;
        thr->time = thr->timeslice;

        queue_thread_ready(thr);

        kern->prepare_reschedule();

        return true;
    }

    void thread_scheduler::unschedule(kernel::thread *thr) {
        if (thr->scheduler_link.next == nullptr && thr->scheduler_link.previous == nullptr) {
            return;
        }

        dequeue_thread_from_ready(thr);
    }

    bool thread_scheduler::stop(kernel::thread *thr) {
        if (wakeup_evt)
            timing->unschedule_event(wakeup_evt, thr->unique_id());

        if (thr->state == thread_state::ready || thr->state == thread_state::run) {
            unschedule(thr);
        } else if (thr->state == thread_state::wait || thr->state == thread_state::wait_fast_sema) {
            if (thr->scheduler_link.next == nullptr && thr->scheduler_link.previous == nullptr) {
                return false;
            }

            dequeue_thread_from_ready(thr);
        }

        thr->state = thread_state::stop;

        if (crr_thread == thr) {
            kern->prepare_reschedule();
        }

        return true;
    }
}
