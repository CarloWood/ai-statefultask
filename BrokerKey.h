#pragma once

#include <boost/intrusive_ptr.hpp>
#include <iosfwd>
#include <cstdint>

class AIStatefulTask;

namespace statefultask {

// BrokerKey
//
// Base class for objects that are used to initialize brokered tasks
// and to initialize them.
//
class BrokerKey
{
 public:
  struct Deleter { Deleter(bool owner = true) : owner(owner) { } void operator()(BrokerKey const* ptr) const { if (owner) delete ptr; } bool owner; };
  using unique_ptr = std::unique_ptr<BrokerKey, Deleter>;
  using const_unique_ptr = std::unique_ptr<BrokerKey const, Deleter>;

  virtual ~BrokerKey() = default;

  virtual uint64_t hash() const = 0;
  virtual void initialize(boost::intrusive_ptr<AIStatefulTask> task) const = 0;
  virtual unique_ptr copy() const = 0;
  virtual void print_on(std::ostream& os) const = 0;

 public:
  bool equal_to(BrokerKey const& other) const { return typeid(*this) == typeid(other) && equal_to_impl(other); }
  unique_ptr non_owning_ptr() { return unique_ptr(this, false); }
  const_unique_ptr non_owning_ptr() const { return const_unique_ptr(this, false); }

 protected:
  virtual bool equal_to_impl(BrokerKey const&) const = 0;
};

struct BrokerKeyHash
{
  uint64_t operator()(BrokerKey::unique_ptr const& ptr) const { return ptr->hash(); }
};

struct BrokerKeyEqual
{
  bool operator()(BrokerKey::unique_ptr const& left, BrokerKey::unique_ptr const& right) const { return left->equal_to(*right); }
};

} // namespace statefultask
