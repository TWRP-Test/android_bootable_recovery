/*
 * Copyright (C) 2026 The Team Win Recovery Project
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

#pragma once

#include <sys/types.h>

#include <memory>
#include <string>

namespace twrp {

class TwrpPerfManager final {
 public:
  static TwrpPerfManager& Get();

  void Initialize(pid_t render_tid = 0);
  void NotifyInteraction();
  void NotifyFrameActivity();
  void Update();
  int ClampTimeoutMs(int timeout_ms) const;
  void Release();

  std::string BackendName() const;
  bool IsBoosted() const;

  ~TwrpPerfManager();

  TwrpPerfManager(const TwrpPerfManager&) = delete;
  TwrpPerfManager& operator=(const TwrpPerfManager&) = delete;

 private:
  TwrpPerfManager();

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace twrp
