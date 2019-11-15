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
