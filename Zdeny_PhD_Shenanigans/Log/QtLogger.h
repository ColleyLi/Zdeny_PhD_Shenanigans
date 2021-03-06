#pragma once
#include "stdafx.h"

class QtLogger
{
public:
  enum class LogLevel
  {
    Trace,
    Debug,
    Info,
    Success,
    Warning,
    Error
  };

  static void SetTextBrowser(QTextBrowser* textBrowser) { Get().mTextBrowser = textBrowser; }
  static void SetLogLevel(LogLevel logLevel) { Get().mLogLevel = logLevel; }

  template <typename... Args> static void Trace(const std::string& fmt, Args&&... args) { Get().LogMessage(LogLevel::Trace, fmt, args...); }
  template <typename... Args> static void Debug(const std::string& fmt, Args&&... args) { Get().LogMessage(LogLevel::Debug, fmt, args...); }
  template <typename... Args> static void Info(const std::string& fmt, Args&&... args) { Get().LogMessage(LogLevel::Info, fmt, args...); }
  template <typename... Args> static void Success(const std::string& fmt, Args&&... args) { Get().LogMessage(LogLevel::Success, fmt, args...); }
  template <typename... Args> static void Warning(const std::string& fmt, Args&&... args) { Get().LogMessage(LogLevel::Warning, fmt, args...); }
  template <typename... Args> static void Error(const std::string& fmt, Args&&... args) { Get().LogMessage(LogLevel::Error, fmt, args...); }

private:
  QtLogger()
  {
    mColors[LogLevel::Trace] = QColor(150, 150, 150);
    mColors[LogLevel::Debug] = QColor(51, 153, 255);
    mColors[LogLevel::Info] = QColor(205, 255, 0);
    mColors[LogLevel::Success] = QColor(0, 204, 0);
    mColors[LogLevel::Warning] = QColor(255, 154, 20);
    mColors[LogLevel::Error] = QColor(225, 0, 0);
  }

  static QtLogger& Get()
  {
    static QtLogger logger;
    return logger;
  }

  bool ShouldLog(LogLevel logLevel) { return mTextBrowser != nullptr && logLevel >= mLogLevel; }

  static std::string GetCurrentTime()
  {
    time_t now = time(0);
    char buf[sizeof "12:34:56"];
    strftime(buf, sizeof(buf), "%H:%M:%S", localtime(&now));
    return buf;
  }

  static std::string GetLogLevelString(LogLevel logLevel)
  {
    switch (logLevel)
    {
    case LogLevel::Trace:
      return "Trace";
    case LogLevel::Debug:
      return "Debug";
    case LogLevel::Info:
      return "Info";
    case LogLevel::Success:
      return "Success";
    case LogLevel::Warning:
      return "Warning";
    case LogLevel::Error:
      return "Error";
    }
    return "-";
  }

  template <typename... Args> void LogMessage(LogLevel logLevel, const std::string& fmt, Args&&... args)
  {
    std::scoped_lock lock(mMutex);
    if (!ShouldLog(logLevel))
      return;

    mTextBrowser->setTextColor(mColors[logLevel]);
    mTextBrowser->append(fmt::format("[{}] [{}] {}", GetCurrentTime(), GetLogLevelString(logLevel), fmt::format(fmt, args...)).c_str());
    QCoreApplication::processEvents();
  }

  QTextBrowser* mTextBrowser = nullptr;
  LogLevel mLogLevel = LogLevel::Debug;
  std::unordered_map<LogLevel, QColor> mColors;
  std::mutex mMutex;
};
