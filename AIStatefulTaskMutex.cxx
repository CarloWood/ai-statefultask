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

void AIStatefulTaskMutex::signal_next(Node* const owner COMMA_CWDEBUG_ONLY(AIStatefulTask* const task))
{
//    Dout(dc::notice, "m_head not changed it wasn't equal to owner (" << owner << "), m_head is " << expected << " [" << task << "]");
  // Wait until the task that tried to get the lock first (after us) set m_next.
  Node* next;
//next = owner->m_next.load(std::memory_order_acquire);
//    Dout(dc::notice, "owner->m_next = " << next << " [" << task << "]");
  while (!(next = owner->m_next.load(std::memory_order_acquire)))
    cpu_relax();
//    Dout(dc::notice, "deallocating " << owner << " [" << task << "]");
  s_node_memory_resource.deallocate(owner);
  Dout(dc::notice, "Setting m_owner to " << next << " [" << task << "]");
  m_owner.store(next, std::memory_order_release);
  Dout(dc::notice, "Calling signal(" << next->m_condition << ") on next->m_task (" << next->m_task << ") with next = " << next << " [" << task << "]");
  next->m_task->signal(next->m_condition);
//    Dout(dc::notice, "Leaving unlock()");
}

//static
utils::NodeMemoryResource AIStatefulTaskMutex::s_node_memory_resource;
