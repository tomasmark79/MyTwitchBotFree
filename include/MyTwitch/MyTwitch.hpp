// MIT License
// Copyright (c) 2024-2025 Tomáš Mark

#ifndef __MYTWITCH_HPP
#define __MYTWITCH_HPP

#include <MyTwitch/version.h>
#include <filesystem>
#include <string>
#include <memory>
#include <EmojiTools/EmojiTools.hpp>

// Public API

namespace dotname {

  class MyTwitch {

    const std::string libName = std::string ("MyTwitch v.") + MYTWITCH_VERSION;
    std::filesystem::path assetsPath_;
    std::shared_ptr<dotname::EmojiTools> /*💋*/ emojiTools;
    std::string emoji;

  public:
    MyTwitch ();
    MyTwitch (const std::filesystem::path& assetsPath);
    ~MyTwitch ();
    void Bot ();

    const std::filesystem::path getAssetsPath () const {
      return assetsPath_;
    }
    void setAssetsPath (const std::filesystem::path& assetsPath) {
      assetsPath_ = assetsPath;
    }
  };

} // namespace dotname

#endif // __MYTWITCH_HPP