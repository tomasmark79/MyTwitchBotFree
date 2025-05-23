// MIT License
// Copyright (c) 2024-2025 Tomáš Mark

#include <MyTwitch/MyTwitch.hpp>
#include <Assets/AssetContext.hpp>
#include <Logger/Logger.hpp>
#include <Utils/Utils.hpp>

// twith bot engine references:
// https://github.com/jwkblades/TwitchChatBot

#include <curl/curl.h>

#include "BotEngine/Core/CommandSet.h"
#include "BotEngine/Core/ScopeExit.h"
#include "BotEngine/Core/Throttler.h"
#include "BotEngine/Core/Utils.h"
#include "BotEngine/Inet/InetSocket.h"
#include "BotEngine/Threading/PostOffice.h"
#include "nlohmann/json.hpp"
#include <EmojiTools/EmojiTools.hpp>
#include <Sunriset/Sunriset.hpp>

#include <atomic>
#include <fstream>
#include <iostream>
#include <list>
#include <set>
#include <thread>
#include <unistd.h>
#include <ctime>

std::atomic<bool> stopTimerThread;
std::list<std::string> membersInRaffle;
bool raffleOpen = false;
#define EMOJI_INTERVAL_SEC (int)600

CommandSet mCommands;

namespace dotname {

  MyTwitch::MyTwitch () {
    LOG_D_STREAM << libName_ << " constructed ..." << std::endl;
    AssetContext::clearAssetsPath ();
  }

  MyTwitch::MyTwitch (const std::filesystem::path& assetsPath) : MyTwitch () {
    if (!assetsPath.empty ()) {
      AssetContext::setAssetsPath (assetsPath);
      LOG_D_STREAM << "Assets path given to the library\n"
                   << "╰➤ " << AssetContext::getAssetsPath () << std::endl;
      auto logo = std::ifstream (AssetContext::getAssetsPath () / "logo.png");

      this->Bot ();
    }
  }

  MyTwitch::~MyTwitch () {
    LOG_D_STREAM << libName_ << " ... destructed" << std::endl;
  }

  std::string MyTwitch::getCurrentTime () {
    time_t now = time (0);
    struct tm tstruct;
    char buf[80];
    tstruct = *localtime (&now);
    strftime (buf, sizeof (buf), "%Y-%m-%d %X", &tstruct);
    return buf;
  }

  std::string MyTwitch::getSunriset () {
    std::string today = getCurrentTime ();
    std::string year = today.substr (0, 4);
    std::string month = today.substr (5, 2);
    std::string day = today.substr (8, 2);
    int yearInt = std::stoi (year);
    int monthInt = std::stoi (month);
    int dayInt = std::stoi (day);
    double rise = 0.0;
    double set = 0.0;
    sunrisetTools->getSunriset (yearInt, monthInt, dayInt, 14.265802152828646, 49.86396819090531,
                                rise, set);
    return "Sunrise " + sunrisetTools->doubleTo24Time (rise + 2) + " and sunset "
           + sunrisetTools->doubleTo24Time (set + 2) + " for today!";
  }

  void MyTwitch::Bot () {
    this->emojiTools = std::make_shared<dotname::EmojiTools> (AssetContext::getAssetsPath ());

    Address commandParser ("commandParser");
    Address transceiver ("transceiver");
    Address pointAdder ("pointAdder");
    Address apiAddress ("apiServer");

    auto pointAdderLambda = [] (Address& pointAdder, Address& commandParser) -> void {
      Throttler pointIncrementer (std::chrono::seconds (20));
      PostOffice* postOffice = PostOffice::instance ();
      Address myAddress = pointAdder;

      std::set<std::string> members = {};
      nlohmann::json points = nlohmann::json::object ();

      std::ifstream jsonIn ("members.json");
      if (jsonIn.good ()) {
        jsonIn >> points;
      }
      jsonIn.close ();

      std::string command = "";
      while (command != "SHUTDOWN") {
        if (pointIncrementer.check (1)) {
          {
            RAIIMutex memberLock (&members);
            for (const std::string& member : members) {
              int value = 0;
              if (points[member].is_null ()) {
                points[member] = nlohmann::json::object ();
              }
              if (!points[member]["points"].is_null ()) {
                value = points[member]["points"].get<int> ();
              }
              points[member]["points"] = ++value;
            }
          }
          pointIncrementer.addUnit ();

          std::ofstream jsonIn ("members.json");
          if (jsonIn.good ()) {
            jsonIn << points;
          }
          jsonIn.close ();
        }

        Message incomingMsg;
        if (PostOffice::isValidInstance (postOffice) && postOffice->checkMail (myAddress)) {
          incomingMsg = postOffice->getMail (myAddress);
        }
        if (incomingMsg.size () == 0) {
          usleep (300);
          continue;
        }

        command = std::string (incomingMsg.raw (), incomingMsg.size () - 1);
        std::vector<std::string> parts = split (command, " ");
        if (parts.empty ()) {
          usleep (300);
          continue;
        }
        command = parts.at (0);
        parts.erase (parts.begin ());

        if (command == "JOIN" && !parts.empty ()) {
          members.insert (parts.at (0));
          if (points[parts.at (0)].is_null ()) {
            points[parts.at (0)] = nlohmann::json::object ();
          }
          points[parts.at (0)]["role"] = "member";
        } else if (command == "PART" && !parts.empty ()) {
          members.erase (members.find (parts.at (0)));
        } else if (command == "MODE" && parts.size () >= 2) {
          if (points[parts.at (1)].is_null ()) {
            points[parts.at (1)] = nlohmann::json::object ();
          }
          points[parts.at (1)]["role"] = parts.at (0);
        } else if (command == "GET_POINTS" && !parts.empty ()) {
          std::string user = parts.at (0);
          std::string resp = "0";
          if (!points[user].is_null ()) {
            resp = points[user]["points"].dump ();
            Message response (resp);
            incomingMsg.respond (response);
          }
        } else if (command == "GETROLE" && !parts.empty ()) {
          if (points[parts.at (0)].is_null ()) {
            points[parts.at (0)] = nlohmann::json::object ();
          }

          std::string role = "member";
          if (points[parts.at (0)]["role"].is_string ()) {
            role = points[parts.at (0)]["role"].get<std::string> ();
          }
          parts.insert (parts.begin (), role);
          Message msg ("RETURNROLE " + join (parts, " "));
          incomingMsg.respond (msg);
        } else if (command == "DEDUCT_POINTS" && !parts.empty ()) {
          std::string resp = "FAIL";
          if (points[parts.at (0)].is_null ()) {
            points[parts.at (0)] = nlohmann::json::object ();
          } else {
            int value = points[parts.at (0)]["points"].get<int> ();
            if (value >= 5) {
              value -= 5;
              points[parts.at (0)]["points"] = value;
              resp = "SUCCESS";
            }
          }

          Message msg (resp);
          incomingMsg.respond (msg);
        }
      };
    };

    auto commandParserLambda = [] (CommandSet& mCommands, Address& commandParser,
                                   Address& pointAdder, Address& transceiver) -> void {
      PostOffice* postOffice = PostOffice::instance ();
      Address myAddress = commandParser;
      SCOPE_EXIT[&]() {
        cout << "Exiting " << myAddress << endl;
      };

      Throttler namesTimeout (std::chrono::seconds (60));
      std::string command = "";
      std::string prefix = "";
      std::string state = "";
      std::vector<std::string> params;

      while (command != "SHUTDOWN") {
        Message incomingMsg;
        if (PostOffice::isValidInstance (postOffice) && postOffice->checkMail (myAddress)) {
          incomingMsg = postOffice->getMail (myAddress);
        }

        if (incomingMsg.size () == 0) {
          usleep (300);
          continue;
        }
        std::string role = "";
        CommandParts cmdParts = parseCommand (incomingMsg);
        if (!cmdParts.valid) {
          continue;
        }
        command = cmdParts.command;
        prefix = cmdParts.prefix;
        state = cmdParts.state;
        params = cmdParts.params;

        cout << "State='" << state << "' Prefix='" << prefix << "' Command='" << command
             << "' Params=['" << join (params, "', '") << "']" << endl;

        if (command == "PING") {
          Message msg ("PONG " + join (params, " ") + "\r\n");
          msg.send (myAddress, transceiver);
        } else if (command == "PRIVMSG") {
          std::string user = split (prefix.substr (1), "!").at (0);
          std::string channel = params.front ();
          params.erase (params.begin ());

          mCommands.run (user, channel, params);
        } else if (command == "JOIN" || command == "PART") {
          std::string user = split (prefix.substr (1), "!").at (0);
          Message msg (command + " " + user);
          msg.send (myAddress, pointAdder);
        } else if (command == "MODE" && params.size () >= 3) {
          Message msg ("MODE " + params.at (1) + " " + params.at (2));
          msg.send (myAddress, pointAdder);
        } else if (command == "376") // the end of the MOTD message.
        {
          Message msg ("CAP REQ twitch.tv/commands\r\n");
          msg.send (myAddress, transceiver);
          msg = Message ("CAP REQ twitch.tv/membership\r\n");
          msg.send (myAddress, transceiver);
          msg = Message ("CAP REQ twitch.tv/tags\r\n");
          msg.send (myAddress, transceiver);
          msg = Message ("JOIN #digitalspacedotname\r\n");
          msg.send (myAddress, transceiver);
        } else if (command == "353") // members list
        {
          std::vector<std::string> names = split (params.back (), " ");
          for (const std::string& name : names) {
            Message msg ("JOIN " + name);
            msg.send (myAddress, pointAdder);
          }
        } else if (command == "366") // member list finished
        {
          // empty
        }
      }
    };

    auto transceiverLambda = [] (Address& commandParser, Address& transceiver, Address& pointAdder,
                                 Address& apiAddress) -> void {
      Socket twitchConnection;
      int connectionRet = twitchConnection.connect ("irc.chat.twitch.tv", "6667");
      SCOPE_EXIT[&](void)->void {
        twitchConnection.close ();
      };
      cout << "Connection return value: " << connectionRet << endl;

      // https://dev.twitch.tv/docs/authentication/getting-tokens-oauth/

      // fixme
      ifstream fileIn ("/home/tomas/.tokens/.twitch_oauth.key");
      std::string oauthToken = "";
      if (!fileIn.is_open ()) {
        cout << "ERROR: Unable to open oauth file..." << endl;
      }
      fileIn >> oauthToken;
      fileIn.close ();

      Message pass ("PASS " + oauthToken + "\r\n");
      pass.send (transceiver, transceiver);
      Message nick ("NICK digitalspacedotname\r\n");
      nick.send (transceiver, transceiver);

      // This is where the real body of the transceiver goes, now that we have
      // an open connection to twitch IRC.
      const int bufferLength = 1024;
      char* buffer = new char[bufferLength];
      Throttler throttle;
      PostOffice* postOffice = PostOffice::instance ();
      Address myAddress = transceiver;
      int messagesPerTick = 20;

      SCOPE_EXIT[&](void)->void {
        delete[] buffer;
        Message msg ("SHUTDOWN");
        msg.send (myAddress, commandParser);
        msg.send (myAddress, pointAdder);
        msg.send (myAddress, apiAddress);
        cout << "Exiting " << myAddress << endl;
      };

      std::string incompleteMessage = "";
      while (twitchConnection.connected ()) {
        if (PostOffice::isValidInstance (postOffice) && postOffice->checkMail (myAddress)
            && throttle.check (messagesPerTick)) {
          Message receivedMessage = postOffice->getMail (myAddress);
          std::size_t size = receivedMessage.size ();
          std::size_t sentBytes = 0;
          const char* ptr = receivedMessage.raw ();
          std::string s (ptr, size);

          if (s.find ("PASS") == std::string::npos) {
            cout << "Sending message: '" << ptr << "' to twitch." << endl;
          }

          if (s.find ("SHUTDOWN") == 0) {
            cout << "CLOSING CONNECTION TO TWITCH." << endl;
            twitchConnection.close ();
            break;
          }

          do {
            int sent = twitchConnection.send (ptr + sentBytes, size - sentBytes, 100);

            if (sent < 0) {
              cout << "Something bad happened while sending message" << endl;
              break;
            }

            sentBytes += sent;
          } while (sentBytes < size);
          throttle.addUnit ();
        }

        if (messagesPerTick == 20) // The base number.
        {
          Message msg ("GETROLE digitalspacedotname NOOP");
          Message response = msg.sendAndAwaitResponse (commandParser, pointAdder);
          if (response.size () == 0) {
            continue;
          }
          CommandParts cmdParts = parseCommand (response);
          if (!cmdParts.valid) {
            continue;
          }

          std::vector<std::string> parms = cmdParts.params;
          std::string role = parms.at (0);

          if (role == "+o") {
            messagesPerTick = 100;
            cout << "!!! We have op on channel, so increasing to 100 "
                    "messages per 30 "
                    "seconds."
                 << endl;
          }
        }

        int receivedBytes = twitchConnection.receive (buffer, bufferLength - 1);
        if (receivedBytes == 0) {
          usleep (30);
          continue;
        }
        buffer[receivedBytes] = '\0';

        std::string currentMessage = "";
        std::stringstream s (buffer);
        do {
          std::getline (s, currentMessage, '\n');
          incompleteMessage += currentMessage;

          if (!incompleteMessage.empty () && incompleteMessage.back () == '\r') {
            incompleteMessage = incompleteMessage.substr (0, incompleteMessage.size () - 1);
            Message msg (incompleteMessage);
            msg.send (myAddress, commandParser);
            incompleteMessage = "";
          }
        } while (!s.eof ());
      }
    };

    auto apiServerLambda = [] (Address& transceiver, Address& apiAddress) -> void {
      Address myAddress = apiAddress;
      Socket server;
      std::list<Socket*> clients;
      std::size_t bufferLength = 1024;
      char* buffer = new char[bufferLength];
      int ret = 0;

      SCOPE_EXIT[&](void)->void {
        delete[] buffer;

        server.close ();
        for (Socket*& client : clients) {
          delete client;
          client = nullptr;
        }
      };

      do {
        if (ret == EAGAIN) {
          usleep (500000);
        }
        ret = server.bind ("3000", "127.0.0.1");
      } while (ret == EAGAIN);

      while (server.connected ()) {
        if (PostOffice::instance ()->checkMail (myAddress)) {
          cout << "########## API Server closing down" << endl;
          break;
        }

        Socket* client = server.accept (false);
        if (client && client->connected ()) {
          clients.push_back (client);
        } else if (clients.empty ()) {
          usleep (500);
          continue;
        }

        client = clients.front ();
        clients.pop_front ();
        if (!client->connected ()) {
          delete client;
          continue;
        }

        int receivedBytes = client->receive (buffer, bufferLength - 1);
        clients.push_back (client);

        if (receivedBytes == 0) {
          continue;
        }
        buffer[receivedBytes] = '\0';

        std::string m (buffer, receivedBytes);
        std::string message;
        cout << "API server received '" << m << "'" << endl;
        if (m == "SHUTDOWN") {
          message = m;
        } else if (m.find (" ") == std::string::npos) {
          message = "PRIVMSG #digitalspacedotname " + m + "\r\n";
        } else {
          message = "PRIVMSG #digitalspacedotname :" + m + "\r\n";
        }
        Message msg (message);
        msg.send (myAddress, transceiver);
      }
    };

    mCommands.registerCommand (
        { "!startemojitimer", "Start Random Emoji Timer!",
          [&] (const std::string& user, const std::string& channel,
               const std::vector<std::string>& params) -> void {
            stopTimerThread.store (false);
            std::thread threadTimer (
                [this] (Address commandParser, Address transceiver) -> void {
                  while (!stopTimerThread.load ()) {
                    std::cout << "startemojitimer" << std::endl;
                    std::string randomEmoji = emojiTools->getRandomEmoji ();
                    Message msg ("PRIVMSG #digitalspacedotname : " + randomEmoji + "\r\n");
                    msg.send (commandParser, transceiver);
                    std::this_thread::sleep_for (std::chrono::seconds (EMOJI_INTERVAL_SEC));
                  }
                },
                commandParser,
                transceiver); // Pass the variables here
            threadTimer.detach ();
          } });

    mCommands.registerCommand (
        { "!stopemojitimer", "Stop Random Emoji Timer!",
          [&] (const std::string& user, const std::string& channel,
               const std::vector<std::string>& params) -> void { stopTimerThread.store (true); } });

    mCommands.registerCommand ({ "!help", "print this help message.",
                                 [&] (const std::string& user, const std::string& channel,
                                      const std::vector<std::string>& params) -> void {
                                   std::vector<std::string> commands = mCommands.help ();
                                   for (const std::string& cmd : commands) {
                                     Message msg ("PRIVMSG #digitalspacedotname : " + cmd + "\r\n");
                                     msg.send (commandParser, transceiver);
                                   }
                                 } });

    mCommands.registerCommand ({ "!sunriset", "return Sunrise Sunset",
                                 [&] (const std::string& user, const std::string& channel,
                                      const std::vector<std::string>& params) -> void {
                                   std::string strSunriset = getSunriset ();
                                   Message msg ("PRIVMSG #digitalspacedotname : " + strSunriset + " "
                                                + "\r\n");
                                   msg.send (commandParser, transceiver);
                                 } });

    mCommands.registerCommand (
        { "!time", "return current time",
          [&] (const std::string& user, const std::string& channel,
               const std::vector<std::string>& params) -> void {
            std::time_t now
                = std::chrono::system_clock::to_time_t (std::chrono::system_clock::now ());
            std::string strTime = std::ctime (&now);
            Message msg ("PRIVMSG #digitalspacedotname : " + strTime + " " + "\r\n");
            msg.send (commandParser, transceiver);
          } });

    mCommands.registerCommand (
        { "!emoji", "return random emoji",
          [&] (const std::string& user, const std::string& channel,
               const std::vector<std::string>& params) -> void {
            std::string randomEmoji = emojiTools->getRandomEmojiFromGroup ("Smileys & Emotion");
            Message msg ("PRIVMSG #digitalspacedotname : " + randomEmoji + "\r\n");
            msg.send (commandParser, transceiver);
          } });

    mCommands.registerCommand (
        { "!flag", "return random flag",
          [&] (const std::string& user, const std::string& channel,
               const std::vector<std::string>& params) -> void {
            std::string randomEmoji = emojiTools->getRandomEmojiFromSubGroup ("country-flag");
            Message msg ("PRIVMSG #digitalspacedotname : " + randomEmoji + "\r\n");
            msg.send (commandParser, transceiver);
          } });

    mCommands.registerCommand (
        { "!zodiac", "return random zodiac",
          [&] (const std::string& user, const std::string& channel,
               const std::vector<std::string>& params) -> void {
            std::string randomEmoji = emojiTools->getRandomEmojiFromSubGroup ("zodiac");
            Message msg ("PRIVMSG #digitalspacedotname : " + randomEmoji + "\r\n");
            msg.send (commandParser, transceiver);
          } });

    mCommands.registerCommand (
        { "!gender", "return random gender",
          [&] (const std::string& user, const std::string& channel,
               const std::vector<std::string>& params) -> void {
            std::string randomEmoji = emojiTools->getRandomEmojiFromSubGroup ("gender");
            Message msg ("PRIVMSG #digitalspacedotname : " + randomEmoji + "\r\n");
            msg.send (commandParser, transceiver);
          } });

    mCommands.registerCommand ({ "!shutdown", "shutdown the bot.",
                                 [&] (const std::string& user, const std::string& channel,
                                      const std::vector<std::string>& params) -> void {
                                   if (user == "digitalspacedotname") {
                                     Message msg ("SHUTDOWN");
                                     msg.send (commandParser, transceiver);
                                   }
                                 } });

    mCommands.registerCommand (
        { "!points", "return the number of points you have accrued.",
          [&] (const std::string& user, const std::string& channel,
               const std::vector<std::string>& params) -> void {
            nlohmann::json points = nlohmann::json::object ();
            std::ifstream jsonIn ("members.json");
            if (jsonIn.good ()) {
              jsonIn >> points;
            }
            jsonIn.close ();

            Message pointInfo ("GET_POINTS " + user);
            Message response = pointInfo.sendAndAwaitResponse (commandParser, pointAdder);
            if (response.size () == 0) {
              Message msg ("PRIVMSG " + channel + " :@" + user
                           + ", your currently have no points.\r\n");
              msg.send (commandParser, transceiver);
            }
            std::string pointValue (response.raw (), response.size ());

            cout << "Retrieving points for '" << user << "' on " << channel << endl;
            if (pointValue == "0") {
              Message msg ("PRIVMSG " + channel + " :@" + user
                           + ", your currently have no points.\r\n");
              msg.send (commandParser, transceiver);
            } else {
              Message msg ("PRIVMSG " + channel + " :@" + user + ", your point total is "
                           + pointValue + ".\r\n");
              msg.send (commandParser, transceiver);
            }
          } });

    mCommands.registerCommand (
        { "!startRaffle", "start a point raffle.",
          [&] (const std::string& user, const std::string& channel,
               const std::vector<std::string>& params) -> void {
            Message msg ("GETROLE " + user + " PRIVMSG " + join (params, " "));
            Message response = msg.sendAndAwaitResponse (commandParser, pointAdder);
            if (response.size () == 0) {
              return;
            }
            CommandParts cmdParts = parseCommand (response);
            if (!cmdParts.valid) {
              return;
            }

            std::string command = cmdParts.command;
            std::string prefix = cmdParts.prefix;
            std::vector<std::string> parms = cmdParts.params;
            std::string role = parms.at (0);
            cout << "=== Prefix='" << prefix << "' Command='" << command << "' Params=['"
                 << join (parms, "', '") << "'], Role='" << role << "'" << endl;

            if (role == "+o") {
              raffleOpen = true;
              Message msg ("PRIVMSG " + channel
                           + " :The raffle is now open! Send '!joinRaffle' (no quotes) "
                             "to join.\r\n");
              msg.send (commandParser, transceiver);
            } else {
              Message msg ("PRIVMSG " + channel + " :@" + user
                           + ", you don't have permissions to execute that command.\r\n");
              msg.send (commandParser, transceiver);
            }
          } });

    mCommands.registerCommand (
        { "!joinRaffle", "join a raffle in progress.",
          [&] (const std::string& user, const std::string& channel,
               const std::vector<std::string>& params) -> void {
            if (!raffleOpen) {
              Message msg ("PRIVMSG " + channel + " :@" + user
                           + ", The raffle is not currently open.\r\n");
              msg.send (commandParser, transceiver);
              return;
            }
            /*
           * NOTE This isn't done per-say. It has a drastic flaw in that it
           * doesn't actually cost points to join the raffle. We will want to
           * change that. But given that I don't really like the way the
           * asynchronous messaging is working out with the GETROLE side of
           * things, I am going to take some time to architect this and come
           * back with a better option.
           */
            Message msg ("DEDUCT_POINTS " + user);
            Message response = msg.sendAndAwaitResponse (commandParser, pointAdder);
            if (response.size () == 0) {
              return;
            }
            CommandParts cmdParts = parseCommand (response);
            if (!cmdParts.valid) {
              return;
            }
            std::string command = cmdParts.command;
            std::string prefix = cmdParts.prefix;
            std::vector<std::string> parms = cmdParts.params;
            if (command == "SUCCESS") {
              membersInRaffle.push_back (user);
              Message msg ("PRIVMSG " + channel + " :@" + user
                           + ", you are now in the raffle.\r\n");
              msg.send (commandParser, transceiver);
            } else {
              Message msg ("PRIVMSG " + channel + " :@" + user
                           + ", Sorry, but the command timed out... Please try "
                             "again.\r\n");
              msg.send (commandParser, transceiver);
            }
          } });

    // Lambdas called with std::ref() to pass by reference.
    std::list<std::thread> threads;

    threads.push_back (std::thread (transceiverLambda, std::ref (commandParser),
                                    std::ref (transceiver), std::ref (pointAdder),
                                    std::ref (apiAddress)));

    threads.push_back (std::thread (commandParserLambda, std::ref (mCommands),
                                    std::ref (commandParser), std::ref (pointAdder),
                                    std::ref (transceiver)));

    threads.push_back (
        std::thread (pointAdderLambda, std::ref (pointAdder), std::ref (commandParser)));

    threads.push_back (
        std::thread (apiServerLambda, std::ref (transceiver), std::ref (apiAddress)));

    for (std::thread& t : threads) {
      t.join ();
    }

    PostOffice::finalize ();
  }

} // namespace dotname