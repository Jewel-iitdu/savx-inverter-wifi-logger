// Lock.h - minimal lock interface so host-portable containers can be shared between FreeRTOS tasks.
#pragma once

struct ILock {
  virtual ~ILock() {}
  virtual void lock() = 0;
  virtual void unlock() = 0;
};

class LockGuard {
 public:
  explicit LockGuard(ILock* l) : l_(l) { if (l_) l_->lock(); }
  ~LockGuard() { if (l_) l_->unlock(); }
  LockGuard(const LockGuard&) = delete;
  LockGuard& operator=(const LockGuard&) = delete;

 private:
  ILock* l_;
};
