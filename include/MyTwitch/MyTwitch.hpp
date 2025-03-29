#ifndef __MYTWITCH_H__
#define __MYTWITCH_H__

// MIT License
// Copyright (c) 2024-2025 Tomáš Mark

#include <string>
// Public API

namespace library
{

  class MyTwitch
  {
  public:
    MyTwitch (const std::string &assetsPath);
    ~MyTwitch ();

    // alternatively, you can use a getter function
    const std::string getAssetsPath () const { return m_assetsPath; }

  private:
    std::string m_assetsPath;
  };

} // namespace library

#endif // __MYTWITCH_H__
