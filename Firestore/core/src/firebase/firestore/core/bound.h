/*
 * Copyright 2019 Google
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef FIRESTORE_CORE_SRC_FIREBASE_FIRESTORE_CORE_BOUND_H_
#define FIRESTORE_CORE_SRC_FIREBASE_FIRESTORE_CORE_BOUND_H_

#include <iosfwd>
#include <string>
#include <utility>
#include <vector>

#include "Firestore/core/src/firebase/firestore/core/order_by.h"
#include "Firestore/core/src/firebase/firestore/model/document.h"
#include "Firestore/core/src/firebase/firestore/model/field_value.h"

namespace firebase {
namespace firestore {
namespace core {

/**
 * Bound represents the starting or ending position in query results.
 *
 * The bound is specified with components representing a position in the results
 * and whether it's just before or just after the position (relative to whatever
 * the query order is).
 *
 * The position represents a logical index position for a query. It's a prefix
 * of values for the (potentially implicit) order by clauses of a query.
 *
 * Bound provides a function to determine whether a document comes before or
 * after a bound. This is influenced by whether the position is just before or
 * just after the provided values.
 */
class Bound {
 public:
  /**
   * Creates a new bound.
   *
   * @param position The position relative to the sort order.
   * @param is_before Whether this bound is just before or just after the
   *     position.
   */
  Bound(std::vector<model::FieldValue> position, bool is_before)
      : position_(std::move(position)), before_(is_before) {
  }

  /**
   * The index position of this bound represented as an array of field values.
   */
  const std::vector<model::FieldValue>& position() const {
    return position_;
  }

  /** Whether this bound is just before or just after the provided position */
  bool before() const {
    return before_;
  }

  /**
   * Returns true if the given document comes before this bound using the
   * provided sort order.
   */
  bool SortsBeforeDocument(const std::vector<OrderBy>& order_by,
                           const model::Document& document) const;

  std::string CanonicalId() const;

  std::string ToString() const;

  size_t Hash() const;

 private:
  std::vector<model::FieldValue> position_;
  bool before_;
};

std::ostream& operator<<(std::ostream& os, const Bound& bound);

bool operator==(const Bound& lhs, const Bound& rhs);

inline bool operator!=(const Bound& lhs, const Bound& rhs) {
  return !(lhs == rhs);
}

}  // namespace core
}  // namespace firestore
}  // namespace firebase

#endif  // FIRESTORE_CORE_SRC_FIREBASE_FIRESTORE_CORE_BOUND_H_
