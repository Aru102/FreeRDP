using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Moq;
using Nito.Disposables;
using System.Collections.Concurrent;
using UiPath.Rdp;
using UiPath.SessionTools;

namespace UiPath.FreeRdp.Tests;

public class LoggingTests : TestsBase
{
    private readonly ConcurrentDictionary<string, ConcurrentBag<(LogLevel logLevel, string message)>> _logsByCategory = new();
    private readonly ConcurrentBag<object> _scopes = new();
    private readonly Mock<ILogger> _loggerMock = new();

    private async Task<IAsyncDisposable> Connect(RdpConnectionSettings connectionSettings)
    {
        return await Host.Connect(connectionSettings);
    }

    public LoggingTests(ITestOutputHelper output) : base(output)
    {
        var logsByCategoryProvider = new Mock<ILoggerProvider>();
        logsByCategoryProvider.Setup(p => p.CreateLogger(It.IsAny<string>()))
            .Returns((string category) => new FakeLogger(_logsByCategory.GetOrAdd(category, c => new())));
        Host.AddRegistry(s => s.AddLogging(b => b.AddProvider(logsByCategoryProvider.Object)));

        var scopesProvider = new Mock<ILoggerProvider>();
        scopesProvider.Setup(p => p.CreateLogger(It.IsAny<string>()))
            .Returns((string category) => _loggerMock.Object);
        Host.AddRegistry(s => s.AddLogging(b => b.AddProvider(scopesProvider.Object)));
        _loggerMock.Setup(l => l.BeginScope(It.IsAny<It.IsAnyType>())).Callback((object o) => _scopes.Add(o));
    }

    private class FakeLogger : ILogger
    {
        private readonly ConcurrentBag<(LogLevel logLevel, string message)> _logsBag;

        public FakeLogger(ConcurrentBag<(LogLevel logLevel, string message)> logsBag)
        {
            _logsBag = logsBag;
        }

        public IDisposable BeginScope<TState>(TState state)
        => NoopDisposable.Instance;

        public bool IsEnabled(LogLevel logLevel) => true;

        public void Log<TState>(LogLevel logLevel, EventId eventId, TState state, Exception? exception, Func<TState, Exception?, string> formatter)
        => _logsBag.Add(new(logLevel, formatter(state, exception)));
    }

    [Fact]
    public async Task ShouldProduceLogsFromFreerdp()
    {
        var user = await Host.GivenUser();
        var connectionSettings = user.ToRdpConnectionSettings();

        await using var sut = await Connect(connectionSettings);
        var sessionId = await Host.FindSession(connectionSettings);
        await WaitFor.Predicate(() => Host.GetWts().QuerySessionInformation(sessionId).ConnectState()
                    is Windows.Win32.System.RemoteDesktop.WTS_CONNECTSTATE_CLASS.WTSActive
                    or Windows.Win32.System.RemoteDesktop.WTS_CONNECTSTATE_CLASS.WTSConnected);

        await sut.DisposeAsync();
        await Host.WaitNoSession(connectionSettings);

        const string acceptedDebugCategory = "com.freerdp.core.nego";
        var negoLogs = _logsByCategory.Where(kv => kv.Key.StartsWith(acceptedDebugCategory))
            .SelectMany(kv => kv.Value)
            .Where(l => l.logLevel == LogLevel.Debug)
            .ToArray();
        negoLogs.ShouldNotBeEmpty();

        const string wrapperCategory = "UiPath.FreeRdpWrapper";
        var wrapperLogs = _logsByCategory.Where(kv => kv.Key.StartsWith(wrapperCategory))
            .SelectMany(kv => kv.Value)
            .ToArray();
        wrapperLogs.ShouldNotBeEmpty();

        const string freeRdpLoggingCategory = "UiPath.FreeRdpLogging";
        var nonDebugFreeRdpLogs = _logsByCategory.Where(kv => kv.Key.StartsWith(freeRdpLoggingCategory))
            .SelectMany(kv => kv.Value)
            .ToArray();
        nonDebugFreeRdpLogs.ShouldNotBeEmpty();
        nonDebugFreeRdpLogs.ShouldAllBe(l => l.logLevel == LogLevel.Information);
        nonDebugFreeRdpLogs.ShouldContain(l => l.message.Contains("forwardFreeRdpLogs:true"));

        var scopes = _scopes.OfType<IReadOnlyList<KeyValuePair<string, object?>>>()
            .Where(kvl => kvl.Any(kv => kv.Key == NativeLoggingForwarder.ScopeName && connectionSettings.ScopeName.Equals(kv.Value)))
            .ToArray();
        scopes.ShouldNotBeEmpty();
    }

    [Fact]
    public async Task ErrorLogsShouldBeFilteredAndTranslatedToWarn()
    {
        var forwarder = Host.GetRequiredService<NativeLoggingForwarder>();
        forwarder.FilterRemoveStartsWith = new[] { Guid.NewGuid().ToString(), Guid.NewGuid().ToString(), Guid.NewGuid().ToString() };
        forwarder.FilterRemoveStartsWithWarnings = new[] { Guid.NewGuid().ToString(), Guid.NewGuid().ToString(), Guid.NewGuid().ToString() };

        var someTestCategory = Guid.NewGuid().ToString();
        var testLogs = _logsByCategory.Where(kv => kv.Key == someTestCategory)
            .SelectMany(kv => kv.Value);

        foreach (var startWith in forwarder.FilterRemoveStartsWith)
        {
            forwarder.LogCallbackDelegate(someTestCategory, LogLevel.Error, startWith + "_extra1");
            forwarder.LogCallbackDelegate(someTestCategory, LogLevel.Error, startWith + "_extra2");
            forwarder.LogCallbackDelegate(someTestCategory, LogLevel.Error, startWith);
        }
        foreach (var startWith in forwarder.FilterRemoveStartsWithWarnings)
        {
            forwarder.LogCallbackDelegate(someTestCategory, LogLevel.Warning, startWith + "_extra1");
            forwarder.LogCallbackDelegate(someTestCategory, LogLevel.Warning, startWith + "_extra2");
            forwarder.LogCallbackDelegate(someTestCategory, LogLevel.Warning, startWith);
        }
        testLogs.ShouldBeEmpty();

        foreach (var startWith in forwarder.FilterRemoveStartsWith)
        {
            forwarder.LogCallbackDelegate(someTestCategory, LogLevel.Error, "_" + startWith);
        }
        foreach (var startWith in forwarder.FilterRemoveStartsWithWarnings)
        {
            forwarder.LogCallbackDelegate(someTestCategory, LogLevel.Warning, "_" + startWith);
        }
        testLogs.Count()
            .ShouldBe(forwarder.FilterRemoveStartsWith.Length + forwarder.FilterRemoveStartsWithWarnings.Length);
        testLogs.Count(l => l.logLevel is LogLevel.Warning)
            .ShouldBe(forwarder.FilterRemoveStartsWith.Length + forwarder.FilterRemoveStartsWithWarnings.Length);
        testLogs.Where(l => l.logLevel is LogLevel.Error)
            .ShouldBeEmpty();
    }
}