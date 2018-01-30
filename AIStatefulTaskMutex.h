/**
 * @file
 * @brief Mutex for stateful tasks. Declaration of class AIStatefulTaskMutex.
 *
 * Copyright (C) 2016, 2017  Carlo Wood.
 *
 * RSA-1024 0x624ACAD5 1997-01-26                    Sign & Encrypt
 * Fingerprint16 = 32 EC A7 B6 AC DB 65 A6  F6 F6 55 DD 1C DC FF 61
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * CHANGELOG
 *   and additional copyright holders.
 *
 *   12/12/2016
 *   - Initial version, written by Aleric Inglewood @ SL
 *
 *   2017/01/07
 *   - Changed license to Affero GPL.
 *   - Transfered copyright to Carlo Wood.
 */

#pragma once

#include "aithreadsafe.h"

class AIStatefulTask;

// class AIStatefulTaskMutex

class AIStatefulTaskMutex
{
 protected:
  AIStatefulTask const* m_owner;              // Owner of the lock. Only valid when m_lock_count > 0.
  AIThreadSafeSimpleDC<int> m_lock_count;     // Number of times unlock must be callled before unlocked.
  using lock_count_wat = AIAccess<int>;
  using lock_count_crat = AIAccessConst<int>;

 public:
  AIStatefulTaskMutex() : m_owner(nullptr), m_lock_count(0) { }

  bool trylock(AIStatefulTask const* owner)
  {
    lock_count_wat lock_count_w(m_lock_count);
    if (*lock_count_w > 0 && m_owner != owner) return false;
    m_owner = owner;
    ++*lock_count_w;
    return true;
  }
  void unlock(AIStatefulTask const* owner)
  {
    lock_count_wat lock_count_w(m_lock_count);
    ASSERT(*lock_count_w > 0 && m_owner == owner);
    --*lock_count_w;
  }
  bool is_locked() const
  {
    lock_count_crat lock_count_w(m_lock_count);
    return *lock_count_w > 0;
  }
};
