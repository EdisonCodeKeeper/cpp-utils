#pragma once

#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <set>
#include <stdint.h>
#include "logging.h"
#include "tinydatatypes.h"

namespace TinyRPC
{
    typedef std::lock_guard<std::mutex> LockGuard;

    template<class Response>
    class SleepingList
    {
        struct ResponseSignaled
        {
            ResponseSignaled()
            : response(nullptr),
            received(false),
            server_failure(false){}

            Response * response;
            bool received;
            bool server_failure;
            std::condition_variable cv;
        };
    public:
        SleepingList(){}

        void add_event(int64_t event, Response * r)
        {
            LockGuard l(_lock);
            ResponseSignaled *& rs = _event_map[event];
            ASSERT(rs == nullptr, "event already registered");
            rs = new ResponseSignaled();
            rs->response = r;
        }

        void remove_event(int64_t event)
        {
            LockGuard l(_lock);
            remove_event_locked(event);
        }

        /// <summary>
        /// wait until the response has arrived or timeout has reached.
        /// </summary>
        /// <param name="event">The sequence id of the request.</param>
        /// <param name="timeout">The timeout in milliseconds, default 0 indicats infinity.</param>
        /// <returns>error code</returns>
        TinyErrorCode wait_for_response(int64_t event, uint64_t timeout = 0)
        {
            std::unique_lock<std::mutex> l(_lock);
            TinyErrorCode ret = TinyErrorCode::SUCCESS;
            ResponseSignaled * rs = _event_map[event];
            ASSERT(rs->response != nullptr, "null response pointer");
            if (!rs->received)
            {
                bool out_of_time = false;
                if (timeout == 0)
                {
                    // wait forever
                    rs->cv.wait(l);
                }
                else
                {
                    std::cv_status s =
                        rs->cv.wait_for(l, std::chrono::milliseconds(timeout));
                    if (s == std::cv_status::timeout)
                    {
                        out_of_time = true;
                    }
                }
                if (out_of_time)
                {
                    ret = TinyErrorCode::TIMEOUT;
                }
                else if (rs->server_failure)
                {
                    ret = TinyErrorCode::SERVER_FAIL;
                }
            }
            
            remove_event_locked(event);
            return ret;
        }

        Response * get_response_ptr(int64_t event)
        {
            LockGuard l(_lock);
            auto it = _event_map.find(event);
            if (it == _event_map.end())
            {
                // could have timed out and deleted
                return nullptr;
            }
            else
            {
                return it->second->response;
            }
        }

        void signal_response(int64_t event)
        {
            LockGuard l(_lock);
            auto it = _event_map.find(event);
            if (it == _event_map.end())
            {
                // could have timed out and deleted
                return;
            }
            else
            {
                it->second->received = true;
                it->second->cv.notify_one();
            }
        }

        void signal_server_fail(int64_t event)
        {
            LockGuard l(_lock);
            auto it = _event_map.find(event);
            if (it == _event_map.end())
            {
                // could have timed out and deleted
                return;
            }
            else
            {
                it->second->received = false;
                it->second->server_failure = true;
                it->second->cv.notify_one();
            }
        }
    private:
        void remove_event_locked(int64_t event)
        {
            // assuming lock is held
            auto it = _event_map.find(event);
            delete it->second;
            _event_map.erase(it);
        }

        std::map<int64_t, ResponseSignaled*> _event_map;
        std::mutex _lock;
    };

};

