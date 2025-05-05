// MIT License
// Copyright (c) 2024-2025 Tomáš Mark

#ifndef __MYTWITCH_HPP
#define __MYTWITCH_HPP

#include <MyTwitch/version.h>
#include <filesystem>
#include <string>
#include <memory>
#include <EmojiTools/EmojiTools.hpp>
#include <Sunriset/Sunriset.hpp>

// Public API

namespace dotname {

  class MyTwitch {

    const std::string libName_ = std::string ("MyTwitch v.") + MYTWITCH_VERSION;
    std::shared_ptr<dotname::EmojiTools> /*💋*/ emojiTools;
    std::string emoji;
    std::shared_ptr<dotname::Sunriset> sunrisetTools;
    std::string getCurrentTime ();
    std::string getSunriset ();
  public:
    MyTwitch ();
    MyTwitch (const std::filesystem::path& assetsPath);
    ~MyTwitch ();
    void Bot ();
  };

} // namespace dotname

#endif // __MYTWITCH_HPP