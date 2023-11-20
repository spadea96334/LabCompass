#include "logwatcher.h"

#ifdef Q_OS_WIN
#include "windows.h"
#include "psapi.h"
#endif

static const QStringList START_LINES {
  "伊澤洛: 精確的上升。",
  "伊澤洛: 女神在看著。",
  "伊澤洛: 正義將會獲得伸張。",
};
static const QStringList FINISH_LINES {
  "伊澤洛: 我為了帝國而死！",
  "伊澤洛: 在鍍金的地牢中感到喜悅吧，昇華者。",
  "伊澤洛: 終點比你的旅途更加危險，昇華者。",
  "伊澤洛: 終於勝利了！",
  "伊澤洛: 你自由了！",
  "伊澤洛: 暴政的陷阱是不可避免的。",
};
static const QStringList IZARO_BATTLE_START_LINES {
  "伊澤洛: 各種複雜的詭計結合為單一的一種力量。",
  "伊澤洛: 緩慢會將力量借給敵人。",
  "伊澤洛: 汙穢雕像等同於汙穢帝王。",
  "伊澤洛: 帝國的精華必須讓所有人民平均享用。",
  "伊澤洛: 是霸主賦予權杖力量，而不是相反。",
  "伊澤洛: 有些在沉睡的東西不該被喚醒。",
  "伊澤洛: 帝王的效率取決於他的部屬。",
  "伊澤洛: 帝王招手而世界來參加。",
};
static const QStringList SECTION_FINISH_LINES {
  "伊澤洛: 女神啊！如此的野心！",
  "伊澤洛: 這樣的韌性！",
  "伊澤洛: 你真是難纏！",
  "伊澤洛: 你是為此而生！",
};
static const QStringList PORTAL_SPAWN_LINES {
  ": A portal to Izaro appears."
};
static const QStringList LAB_ROOM_PREFIX {"Estate", "Domain", "Basilica", "Mansion", "Sepulchre", "Sanitorium"};
static const QStringList LAB_ROOM_PREFIX_TW {"莊園", "領地", "教堂", "宅邸", "陰墓", "安養"};
static const QStringList LAB_ROOM_SUFFIX {"Walkways", "Path", "Crossing", "Annex", "Halls", "Passage", "Enclosure", "Atrium"};
static const QStringList LAB_ROOM_SUFFIX_TW {"走道", "通路", "岔點", "附館", "殿堂", "通道", "勢力", "庭院"};
static const QRegularExpression LOG_REGEX {"^\\d+/\\d+/\\d+ \\d+:\\d+:\\d+.*?\\[.*?(\\d+)\\] (.*)$"};
static const QRegularExpression ROOM_CHANGE_REGEX {"^: 你已進入：(.*?)。$"};

LogWatcher::LogWatcher(ApplicationModel* model)
{
  this->model = model;
  clientPath = model->get_settings()->get_poeClientPath();
  file.reset(new QFile(QDir(clientPath).filePath("logs/Client.txt")));

  timer.setInterval(1000);
  timer.setSingleShot(false);
  timer.start();
  connect(&timer, &QTimer::timeout,
          this, &LogWatcher::work);
}

void LogWatcher::work()
{
  // reset file if client path settings have changed
  auto newClientPath = model->get_settings()->get_poeClientPath();
  if (clientPath != newClientPath) {
    clientPath = newClientPath;
    file.reset(new QFile(QDir(clientPath).filePath("logs/Client.txt")));
  }

  // attempt to open file
  if (!file->isOpen()) {
    if (!file->open(QIODevice::ReadOnly)) {

      // try to detect client
      clientPath = findGameClientPath();
      if (clientPath.isEmpty()) {
        model->update_logFileOpen(false);
        return;
      }
      file.reset(new QFile(QDir(clientPath).filePath("logs/Client.txt")));
      if (!file->open(QIODevice::ReadOnly)) {
        model->update_logFileOpen(false);
        return;
      }
      model->get_settings()->set_poeClientPath(clientPath);
    }
    model->update_logFileOpen(true);
    file->seek(file->size());
  }

  while (true) {
    auto line = file->readLine();
    if (line.isEmpty())
      break;
    parseLine(QString::fromUtf8(line).trimmed());
  }
}

void LogWatcher::parseLine(const QString line)
{
  auto logMatch = LOG_REGEX.match(line);
  if (logMatch.hasMatch()) {
    auto clientId = logMatch.captured(1);

    auto logContent = logMatch.captured(2).trimmed();
    auto roomChangeMatch = ROOM_CHANGE_REGEX.match(logContent);

    if (START_LINES.contains(logContent)) {
      setActiveClient(clientId);
      emit labStarted();

    } else if (roomChangeMatch.hasMatch()) {
      auto roomName = roomChangeMatch.captured(1);
      QStringList affixes;
      if (roomName.length() == 4) {
          affixes << roomName.mid(0, 2);
          affixes << roomName.mid(2, 2);
      }

      if (roomName == "試煉者廣場") {
        setActiveClient(clientId);
        emit plazaEntered();

      } else if (roomName == "昇華試煉" ||
          (affixes.size() == 2 && LAB_ROOM_PREFIX_TW.contains(affixes[0]) && LAB_ROOM_SUFFIX_TW.contains(affixes[1]))) {
        if (isLogFromValidClient(clientId)) {
          if (roomName == "昇華試煉") {
            roomName = QString("Aspirant\'s Trial");
          } else {
              int preIndex = LAB_ROOM_PREFIX_TW.indexOf(affixes[0]);
              int sufIndex = LAB_ROOM_SUFFIX_TW.indexOf(affixes[1]);
              roomName = LAB_ROOM_PREFIX[preIndex] + " " + LAB_ROOM_SUFFIX[sufIndex];
          }

          emit roomChanged(roomName);
        }

      } else if (roomName == "教堂聖殿") {
          emit roomChanged(QString("Basilica Halls"));
      } else {
        if (isLogFromValidClient(clientId))
          emit labExit();
      }

    } else if (FINISH_LINES.contains(logContent)) {
      if (isLogFromValidClient(clientId)) {
        emit sectionFinished();
        emit labFinished();
      }

    } else if (IZARO_BATTLE_START_LINES.contains(logContent)) {
      if (isLogFromValidClient(clientId))
        emit izaroBattleStarted();

    } else if (SECTION_FINISH_LINES.contains(logContent)) {
      if (isLogFromValidClient(clientId))
        emit sectionFinished();

    } else if (PORTAL_SPAWN_LINES.contains(logContent)) {
      if (isLogFromValidClient(clientId))
        emit portalSpawned();
    }
  }
}

QString LogWatcher::findGameClientPath()
{
#ifdef Q_OS_WIN
  auto hwnd = FindWindowA("POEWindowClass", nullptr);
  if (!hwnd)
    return QString();

  DWORD pid;
  GetWindowThreadProcessId(hwnd, &pid);

  auto handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!handle)
    return QString();

  char buf[1024];
  auto r = GetModuleFileNameExA(handle, NULL, buf, 1024);
  QString path = r ? QFileInfo(QString(buf)).dir().absolutePath() : QString();

  CloseHandle(handle);
  return path;
#else
  return QString();
#endif
}

void LogWatcher::setActiveClient(const QString& clientId)
{
  activeClientId = clientId;
}

bool LogWatcher::isLogFromValidClient(const QString& clientId) const
{
  return !model->get_settings()->get_multiclientSupport() || clientId == activeClientId;
}
