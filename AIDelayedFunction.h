/**
 * @file
 * @brief Declaration of AIDelayedFunction, an object storing a function pointer and its arguments.
 *
 * @Copyright (C) 2017  Carlo Wood.
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
 *   21/04/2017
 *   - Initial version, written by Carlo Wood.
 */

#pragma once

#include <functional>
#include "utils/apply_function.h"

#ifndef DOXYGEN
template<typename F>
class AIDelayedFunction; // not defined.
#endif

/*!
 * @brief Helper class for AIPackagedTask.
 *
 * This object reserves storage a function pointer,  its arguments and a return value.
 *
 * The function pointer is initialized at creation,
 * the arguments are added later.
 *
 * The actual invokation of the function happens by calling
 * the \ref invoke() member function, which stores the return
 * value once again in this object.
 *
 * The return value can later be obtained through the member
 * function \ref get().
 *
 * Usage example:
 *
 * @code
 * AIDelayedFunction<char(int, double)> delayed_function(&f);           // char f(int, double);
 * @endcode
 * or
 * @code
 * AIDelayedFunction<char(int, double)> delayed_function(&obj, &C::f);  // char C::f(int, double); where obj is of type C.
 *
 * delayed_function(1, 1.0);            // Store the arguments.
 *
 * delayed_function.invoke();           // Call f(1, 1.0) or obj.f(1, 1.0) and store the result.
 *
 * char c = delayed_function.get();     // Get the result.
 * @endcode
 */
template<typename R, typename ...Args>
class AIDelayedFunction<R(Args...)>
{
 private:
  std::function<R(Args...)> m_function;       // Pointer to the (member) function.
  std::tuple<Args...> m_args;                 // Copy of the arguments to be passed.
  R m_result;                                 // Future result of the function.

 public:
  /*!
   * @brief Construct an AIDelayedFunction for a free function <code>R f(Args...)</code>.
   */
  AIDelayedFunction(R (*fp)(Args...)) { m_function = fp; }

  /*!
   * @brief Construct an AIDelayedFunction for a member function <code>R C::f(Args...)</code> of \a object.
   *
   * The object must have a lifetime that exceeds the call to \ref invoke.
   */
  template<class C>
  AIDelayedFunction(C* object, R (C::*memfn)(Args...))
      { m_function = [object, memfn](Args... args){ (object->*memfn)(args...); }; }

  /*!
   * @brief Exchange the state with that of \a other.
   */
  void swap(AIDelayedFunction& other) noexcept
  {
    m_function.swap(other.m_function);
    m_args.swap(other.m_args);
    std::swap(m_result, other.m_result);
  }

  /*!
   * @brief Store the arguments to be passed.
   */
  void operator()(Args... args) { m_args = std::make_tuple(args...); }

  /*!
   * @brief Actually invoke the call to the stored function with the stored arguments.
   */
  void invoke() { m_result = utils::apply_function(m_function, m_args); }

  /*!
   * @brief Get the result, only valid after invoke was called.
   */
  R const& get() const { return m_result; }
};

/*!
 * @brief Specialization of AIDelayedFunction for functions returning void.
 */
template<typename ...Args>
class AIDelayedFunction<void(Args...)>
{
 private:
  std::function<void(Args...)> m_function;    // Pointer to the (member) function.
  std::tuple<Args...> m_args;                 // Copy of the arguments to be passed.

 public:
  /*!
   * @brief Construct a AIDelayedFunction for a free function.
   */
  AIDelayedFunction(void (*fp)(Args...)) { m_function = fp; }

  /*!
   * @brief Construct a AIDelayedFunction for a member function of object.
   *
   * The object must have a lifetime that exceeds the call to invoke.
   */
  template<class C>
  AIDelayedFunction(C* object, void (C::*memfn)(Args...))
      { m_function = [object, memfn](Args... args){ (object->*memfn)(args...); }; }

  /*!
   * @brief Store the arguments to be passed.
   */
  void operator()(Args... args) { m_args = std::make_tuple(args...); }

  /*!
   * @brief Actually invoke the call to the stored function with the stored arguments.
   */
  void invoke() { utils::apply_function(m_function, m_args); }
};
