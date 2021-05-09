/**
 * ai-statefultask -- Asynchronous, Stateful Task Scheduler library.
 *
 * @file
 * @brief Implementation of AIStatefulTaskMutex.
 *
 * @Copyright (C) 2019  Carlo Wood.
 *
 * RSA-1024 0x624ACAD5 1997-01-26                    Sign & Encrypt
 * Fingerprint16 = 32 EC A7 B6 AC DB 65 A6  F6 F6 55 DD 1C DC FF 61
 *
 * This file is part of ai-statefultask.
 *
 * Ai-statefultask is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Ai-statefultask is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with ai-statefultask.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "sys.h"
#include "AIStatefulTaskMutex.h"
#include "AIStatefulTask.h"

void AIStatefulTaskMutex::unlock()
{
  DoutEntering(dc::notice|flush_cf, "AIStatefulTaskMutex::unlock() [mutex:" << this << "]");

  // We are the owner of the lock, hence there is a node in the queue which is the next to pop.
  Node* owner;
  while (!(owner = static_cast<Node*>(m_queue.pop())))
    cpu_relax();

#ifdef CWDEBUG
  AIStatefulTask* const task = owner->m_task;
  Dout(dc::notice, "Mutex released [" << task << "]");
#endif

  // Free our node.
  s_node_memory_resource.deallocate(owner);

  // Signal the next owner of the lock, if any.
  Node const* next = static_cast<Node const*>(m_queue.peek());

  // If this is false (is_stub is true), then it must be guaranteed
  // that the owner of the front node didn't call push yet, or it did
  // and that push will return True.
  if (next)
  {
    Dout(dc::notice, "The mutex is now held by " << next->m_task << " [" << task << "]");
    next->m_task->signal(next->m_condition);
  }
}

//static
utils::NodeMemoryResource AIStatefulTaskMutex::s_node_memory_resource;
