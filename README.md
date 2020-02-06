# ConstObject

* `Small<T>` object is an easy copyable holder for const objects.
* For small objects there is specialization that holds exact object to reduce the overhad.
* Futhermore, you can mark any classes as easy copyable manually.
* The only one that is marked this way by default is `std::shared_ptr`.
