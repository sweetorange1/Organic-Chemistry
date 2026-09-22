#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <ctime>
#include <memory>
#include <string_view>
#include <utility>

#if JUCE_MAC || JUCE_LINUX || JUCE_BSD
 #include <fcntl.h>
 #include <sys/file.h>
 #include <unistd.h>
#endif

// 目标：JUCE 8.0.7 / 8.0.12，C++17 及以上；不依赖 JuceHeader.h 或项目宏。
// 默认基础遥测，无授权弹窗、无自动更新。只在 Editor/GUI 创建完成后持有 Session。
// 构造和析构必须在 JUCE 消息线程；禁止放入 Processor、音频回调或静态全局对象。
// 定义 IISAAC_TELEMETRY_DISABLED（即使值为 0），或启动前设置同名环境变量为 1，
// 会调试停用计时器、工作线程、状态目录和网络；这不是新增的用户授权 UI。
// 各翻译单元必须使用一致的宏设置，避免 header-only 的 ODR 冲突。

namespace iisaac::telemetry
{
struct Config
{
    juce::String productId;
    juce::String version;
    juce::String buildVersion;
    juce::String pluginMode;
    juce::String pluginFormat;
    juce::String hostName;
};

inline Config forStandalone (const char* id, const char* outwardVersion,
                             const char* buildVersion)
{
    return { id, outwardVersion, buildVersion, "standalone", "Standalone", "Standalone" };
}

inline Config forPlugin (const char* id, const char* outwardVersion,
                         const char* buildVersion, juce::AudioProcessor::WrapperType wrapper)
{
    if (wrapper == juce::AudioProcessor::wrapperType_Standalone)
        return forStandalone (id, outwardVersion, buildVersion);

    juce::String format = "Unknown";
    switch (wrapper)
    {
        case juce::AudioProcessor::wrapperType_VST3:      format = "VST3"; break;
        case juce::AudioProcessor::wrapperType_AudioUnit: format = "AU";   break;
        default: break;
    }

    // 只采用 JUCE 已知宿主的固定描述；未知宿主不回退到路径、进程名或计算机名。
    return { id, outwardVersion, buildVersion, "plugin", format,
             juce::PluginHostType().getHostDescription() };
}

namespace detail
{
inline constexpr char productionEndpoint[] = "https://iisaacbeats.cn/api/telemetry/ping";
inline constexpr int initialDelayMs = 5000;
inline constexpr int dayCheckMs = 60000;
inline constexpr int connectionTimeoutMs = 2000;
inline constexpr int watchdogTimeoutMs = 4000;
inline constexpr juce::int64 dayMs = 24LL * 60 * 60 * 1000;
inline constexpr juce::int64 cooldownMs = 15LL * 60 * 1000;

// 仅接受显式编译的 IPv4 loopback 测试地址；没有运行时 endpoint 配置入口。
constexpr bool isLoopbackTestEndpoint (std::string_view value)
{
    constexpr std::string_view prefix = "http://127.0.0.1:";
    if (value.substr (0, prefix.size()) != prefix)
        return false;

    auto i = prefix.size();
    unsigned port = 0;
    const auto firstDigit = i;
    for (; i < value.size() && value[i] >= '0' && value[i] <= '9'; ++i)
    {
        port = port * 10 + static_cast<unsigned> (value[i] - '0');
        if (port > 65535 || i - firstDigit >= 5)
            return false;
    }
    return i > firstDigit && port != 0 && i < value.size() && value[i] == '/';
}

#if defined(IISAAC_TELEMETRY_TEST_ENDPOINT)
 #if ! defined(IISAAC_TELEMETRY_OFFLINE_TEST)
  #error IISAAC_TELEMETRY_TEST_ENDPOINT_requires_IISAAC_TELEMETRY_OFFLINE_TEST
 #endif
inline constexpr char endpoint[] = IISAAC_TELEMETRY_TEST_ENDPOINT;
static_assert (isLoopbackTestEndpoint (endpoint), "Telemetry test endpoint must be IPv4 loopback");
#else
inline constexpr auto& endpoint = productionEndpoint;
#endif

inline bool isDisabled()
{
#if defined(IISAAC_TELEMETRY_DISABLED)
    return true;
#else
    return juce::SystemStats::getEnvironmentVariable ("IISAAC_TELEMETRY_DISABLED", {}) == "1";
#endif
}

inline bool isValidProductId (const juce::String& id)
{
    // 固定产品标识，不是用户输入；拒绝路径穿越，不通过清洗造成不同产品串号。
    constexpr auto letters = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    return id.isNotEmpty() && id.length() <= 128
        && id.substring (0, 1).containsOnly (letters)
        && id.containsOnly ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-");
}

inline juce::String platform()
{
#if JUCE_WINDOWS
    constexpr auto os = "win";
#elif JUCE_MAC
    constexpr auto os = "mac";
#elif JUCE_LINUX
    constexpr auto os = "linux";
#elif JUCE_BSD
    constexpr auto os = "bsd";
#else
    constexpr auto os = "unknown";
#endif

    // 这是当前二进制的编译架构，不查询硬件；ARM64 必须先于 x64 判断。
#if defined(_M_ARM64) || defined(_M_ARM64EC) || defined(__aarch64__) || defined(__arm64__)
    constexpr auto arch = "arm64";
#elif defined(_M_X64) || defined(__x86_64__) || defined(__amd64__)
    constexpr auto arch = "x64";
#elif defined(_M_ARM) || defined(__arm__)
    constexpr auto arch = "arm";
#elif defined(_M_IX86) || defined(__i386__)
    constexpr auto arch = "x86";
#else
    constexpr auto arch = "unknown";
#endif
    return juce::String (os) + "-" + arch;
}

inline juce::int64 utcDay (juce::int64 milliseconds) noexcept
{
    const auto quotient = milliseconds / dayMs;
    return quotient - (milliseconds % dayMs < 0 ? 1 : 0);
}

inline juce::String utcDayText (juce::int64 milliseconds)
{
    const auto seconds = static_cast<std::time_t> (milliseconds / 1000);
    std::tm utc {};
#if JUCE_WINDOWS
    if (::gmtime_s (&utc, &seconds) != 0)
        return {};
#else
    if (::gmtime_r (&seconds, &utc) == nullptr)
        return {};
#endif
    return juce::String (utc.tm_year + 1900).paddedLeft ('0', 4) + "-"
         + juce::String (utc.tm_mon + 1).paddedLeft ('0', 2) + "-"
         + juce::String (utc.tm_mday).paddedLeft ('0', 2);
}

inline bool isCoolingDown (juce::int64 now, juce::int64 lastAttempt) noexcept
{
    // 时钟回拨仍保守跳过；正常前进跨 UTC 日不继承昨天的冷却期。
    return lastAttempt > 0 && (now <= lastAttempt
        || (utcDay (now) == utcDay (lastAttempt) && now - lastAttempt < cooldownMs));
}

inline bool isValidClientId (const juce::String& value)
{
    const juce::Uuid uuid (value);
    return ! uuid.isNull()
        && (value == uuid.toDashedString() || value == uuid.toString());
}

#if defined(IISAAC_TELEMETRY_TEST_STATE_ROOT) && ! defined(IISAAC_TELEMETRY_OFFLINE_TEST)
 #error IISAAC_TELEMETRY_TEST_STATE_ROOT_requires_IISAAC_TELEMETRY_OFFLINE_TEST
#endif

inline juce::File stateFileFor (const juce::String& productId)
{
#if defined(IISAAC_TELEMETRY_TEST_STATE_ROOT)
    // 编译期离线测试隔离；不读取环境变量，不访问真实用户状态目录。
    auto root = juce::File (juce::String::fromUTF8 (IISAAC_TELEMETRY_TEST_STATE_ROOT));
#else
    auto root = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
 #if JUCE_MAC
    // JUCE 在 macOS 的 userApplicationDataDirectory 是 ~/Library，不是 Application Support。
    root = root.getChildFile ("Application Support");
 #endif
#endif
    return root.getChildFile ("iisaacbeats").getChildFile ("Telemetry")
               .getChildFile (productId + ".xml");
}

inline juce::PropertiesFile::Options stateOptions()
{
    juce::PropertiesFile::Options options;
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    options.millisecondsBeforeSaving = -1;
    options.osxLibrarySubFolder = "Application Support";
    // 不给 PropertiesFile 传 processLock：外围已持有非阻塞锁，不可在内部无限等待。
    options.processLock = nullptr;
    return options;
}

class ProductLock final
{
public:
    ProductLock (const juce::String& productId, [[maybe_unused]] const juce::File& stateFile)
        : processLock ("iisaacbeats.Telemetry." + productId.toLowerCase())
    {
#if JUCE_MAC || JUCE_LINUX || JUCE_BSD
        // JUCE 的 POSIX fcntl 锁按进程持有，不排斥同进程的其他实例/插件模块。
        // 单独 inode 的 flock 补足这一点；必须先拿它，再进入 JUCE 锁。
        const auto lockFile = stateFile.getSiblingFile (stateFile.getFileName() + ".lock");
        descriptor = ::open (lockFile.getFullPathName().toRawUTF8(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
        if (descriptor < 0 || ::flock (descriptor, LOCK_EX | LOCK_NB) != 0)
            return;
#endif
        locked = processLock.enter (0);
    }

    ~ProductLock()
    {
        if (locked)
            processLock.exit();
#if JUCE_MAC || JUCE_LINUX || JUCE_BSD
        if (descriptor >= 0)
            ::close (descriptor); // close 释放 flock；不删除锁文件，避免 inode 竞争。
#endif
    }

    bool isLocked() const noexcept { return locked; }
    ProductLock (const ProductLock&) = delete;
    ProductLock& operator= (const ProductLock&) = delete;

private:
    juce::InterProcessLock processLock;
    bool locked = false;
#if JUCE_MAC || JUCE_LINUX || JUCE_BSD
    int descriptor = -1;
#endif
};

inline bool saveNow (juce::PropertiesFile& state)
{
    bool saved = false;
    // 原子替换返回成功也须回读核对；避免文件系统/过滤驱动留下旧 UUID 或旧日期。
    // 最多三轮本地保存，保持同一份状态；不重新生成 UUID，也不重发 HTTP。
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        if (state.save())
        {
            juce::PropertiesFile persisted (state.getFile(), stateOptions());
            if (persisted.isValidFile() && persisted.getAllProperties() == state.getAllProperties())
            {
                saved = true;
                break;
            }
        }
        if (attempt < 2)
            juce::Thread::sleep (25);
    }
    // 包括失败路径：禁止 PropertiesFile 析构时隐式再写一次。
    state.setNeedsToBeSaved (false);
    return saved;
}

// 内部状态事务。生产调用只发生在 Session::run；分离发送步骤便于纯离线单元测试。
// send 只接收已持久化的随机 UUID，不改变 Session 的固定网络目的地。
template <typename Cancelled, typename Send>
inline void transact (const juce::String& productId, const juce::File& file,
                      juce::int64 now, Cancelled&& cancelled, Send&& send)
{
    if (cancelled() || now <= 0 || ! isValidProductId (productId))
        return;
    if (file.getParentDirectory().createDirectory().failed())
        return;

    ProductLock lock (productId, file);
    if (! lock.isLocked() || cancelled())
        return;

    const auto day = utcDayText (now);
    if (day.isEmpty())
        return;

    const bool existed = file.exists();
    juce::PropertiesFile state (file, stateOptions());
    if (! state.isValidFile())
        return; // 损坏/不可读的已有文件不得被当作新安装重置 UUID。

    auto clientId = state.getValue ("client_id");
    if (existed)
    {
        if (! isValidClientId (clientId)
            || ! state.containsKey ("last_success_day") || ! state.containsKey ("last_attempt_ms"))
            return;

        const auto rawAttempt = state.getValue ("last_attempt_ms");
        const auto lastAttempt = rawAttempt.getLargeIntValue();
        if (lastAttempt < 0 || rawAttempt != juce::String (lastAttempt))
            return;
        if (state.getValue ("last_success_day") == day || isCoolingDown (now, lastAttempt))
            return;
    }
    else
    {
        clientId = juce::Uuid().toDashedString();
        state.setValue ("client_id", clientId);
        state.setValue ("last_success_day", juce::String());
    }

    // 请求前提交 UUID 和尝试时间；磁盘失败就不采 OS、不发送临时身份。
    // PropertiesFile 没有监听器且关闭自动保存，不安排异步 UI 工作。
    state.setValue ("last_attempt_ms", juce::String (now));
    if (! saveNow (state) || cancelled())
        return;

    // 锁持续覆盖加载、保存、发送和成功提交，失败由 last_attempt_ms 留下冷却期。
    if (send (clientId))
    {
        state.setValue ("last_success_day", day);
        (void) saveNow (state);
        // 成功响应但本地提交失败时仍保留之前的尝试时间；绝不在这里重试网络。
    }
}

inline juce::String makePayload (const Config& config, const juce::String& clientId,
                                 const juce::String& osVersion)
{
    auto object = std::make_unique<juce::DynamicObject>();
    object->setProperty ("schema_version", 2);
    object->setProperty ("product_id", config.productId);
    object->setProperty ("client_id", clientId);
    object->setProperty ("version", config.version);
    object->setProperty ("build_version", config.buildVersion);
    object->setProperty ("platform", platform());
    object->setProperty ("os", osVersion);
    object->setProperty ("plugin_mode", config.pluginMode);
    object->setProperty ("plugin_format", config.pluginFormat);
    object->setProperty ("host_name", config.hostName);
    object->setProperty ("event", "ui_open_daily");
    return juce::JSON::toString (juce::var (object.release()), true);
}

class RequestWatchdog final : private juce::Thread
{
public:
    explicit RequestWatchdog (std::shared_ptr<juce::WebInputStream> request)
        : juce::Thread ("iisaac telemetry watchdog"), stream (std::move (request)) {}

    bool start() { return startThread(); }

    ~RequestWatchdog() override
    {
        finished.signal(); // 正常完成提前唤醒，不能每次析构都等足 4 秒。
        waitForThreadToExit (-1);
    }

private:
    void run() override
    {
        if (! finished.wait (watchdogTimeoutMs))
            stream->cancel();
    }

    const std::shared_ptr<juce::WebInputStream> stream;
    juce::WaitableEvent finished;
};
} // namespace detail

class Session final : private juce::Timer, private juce::Thread
{
public:
    explicit Session (Config settings)
        : juce::Thread ("iisaac telemetry"), config (std::move (settings))
    {
        if (detail::isDisabled() || ! detail::isValidProductId (config.productId))
            return;

#if ! (JUCE_WINDOWS || JUCE_MAC || JUCE_LINUX || JUCE_BSD)
        return; // 仅桌面端；例如 Android 的 WebInputStream 无法禁止 redirects。
#endif
        const auto* messages = juce::MessageManager::getInstanceWithoutCreating();
        jassert (messages != nullptr && messages->isThisTheMessageThread());
        if (messages == nullptr || ! messages->isThisTheMessageThread())
            return;

        startTimer (detail::initialDelayMs);
    }

    ~Session() override
    {
        // 与 Timer 回调同在消息线程，避免 stopTimer 不等待正在运行的回调这一限制。
        stopTimer();
        signalThreadShouldExit();
        workAvailable.signal();

        std::shared_ptr<juce::WebInputStream> request;
        {
            const juce::ScopedLock guard (activeLock);
            request = activeStream;
        }
        if (request != nullptr)
            request->cancel(); // 锁外取消，绝不以 activeLock 包住原生网络调用。

        // timeout + cancel 是尽力中断，不是操作系统 API 的绝对总时限。
        // 安全 join 优先：极端网络/文件系统阻塞可能延长 UI 关闭，但不强杀、不 detach，
        // 必须让本 Session 工作线程和它拥有的 watchdog 退出后才允许模块卸载。
        waitForThreadToExit (-1);
    }

    Session (const Session&) = delete;
    Session& operator= (const Session&) = delete;
    Session (Session&&) = delete;
    Session& operator= (Session&&) = delete;

private:
    void timerCallback() override
    {
        startTimer (detail::dayCheckMs);
        const auto day = detail::utcDay (juce::Time::currentTimeMillis());
        // 使用高水位而非仅比较相邻日期，时钟回拨再前进也不会同日重试。
        if (day <= highestScheduledDay || threadShouldExit())
            return;
        highestScheduledDay = day; // 即使锁忙、磁盘/网络失败，当日也已消费调度机会。
        pendingDay.store (day);

        if (! workerStarted)
            workerStarted = startThread();
        if (workerStarted)
            workAvailable.signal();
    }

    void run() override
    {
        while (! threadShouldExit())
        {
            workAvailable.wait (-1);
            if (threadShouldExit())
                break;

            const auto scheduledDay = pendingDay.exchange (-1);
            const auto now = juce::Time::currentTimeMillis();
            if (scheduledDay < 0 || detail::utcDay (now) != scheduledDay)
                continue; // 不补发跨日后才拿到执行机会的旧任务。

            detail::transact (config.productId, detail::stateFileFor (config.productId), now,
                              [this] { return threadShouldExit(); },
                              [this, scheduledDay] (const juce::String& clientId)
                              {
                                  if (threadShouldExit()
                                      || detail::utcDay (juce::Time::currentTimeMillis()) != scheduledDay)
                                      return false;
                                  return post (detail::makePayload (config, clientId,
                                               juce::SystemStats::getOperatingSystemName()));
                              });
        }
    }

    bool post (const juce::String& payload)
    {
        if (threadShouldExit())
            return false;

        auto request = std::make_shared<juce::WebInputStream>
            (juce::URL (detail::endpoint).withPOSTData (payload), true);
        request->withCustomRequestCommand ("POST")
                .withExtraHeaders ("Content-Type: application/json; charset=utf-8\r\n")
                .withConnectionTimeout (detail::connectionTimeoutMs)
                .withNumRedirectsToFollow (0);
        {
            const juce::ScopedLock guard (activeLock);
            if (threadShouldExit())
                return false;
            activeStream = request;
        }

        // 先发布再 connect；RAII 在所有离开路径取消并撤销发布。
        // 此局部对象始终持有一份 shared_ptr，reset 不会在 activeLock 内析构流。
        struct ReleaseActive final
        {
            Session& owner;
            std::shared_ptr<juce::WebInputStream> stream;
            ~ReleaseActive()
            {
                stream->cancel(); // 不读响应体，尽快结束原生后端的接收/缓冲。
                const juce::ScopedLock guard (owner.activeLock);
                owner.activeStream.reset();
            }
        } release { *this, request };

        detail::RequestWatchdog watchdog (request); // 比 release 先析构并 join。
        if (threadShouldExit() || ! watchdog.start())
            return false;
        if (threadShouldExit()) // 处理析构取消与 stream 发布/启动之间的竞态。
            return false;

        if (! request->connect (nullptr))
            return false;

        // 不调用 read/readEntireStream；getStatusCode 在 connect 后不再建立新请求。
        // 本层只调用一次 connect，无自动重试；JUCE/系统自身的传输行为由后端决定。
        const auto status = request->getStatusCode();
        return status >= 200 && status < 300;
    }

    const Config config;
    juce::CriticalSection activeLock;
    std::shared_ptr<juce::WebInputStream> activeStream;
    juce::WaitableEvent workAvailable;
    std::atomic<juce::int64> pendingDay { -1 };
    juce::int64 highestScheduledDay = -1; // 仅消息线程访问。
    bool workerStarted = false;          // 仅消息线程访问。
};
} // namespace iisaac::telemetry
