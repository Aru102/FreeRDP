﻿using Microsoft.Extensions.Logging;
using Newtonsoft.Json;
using System.Collections.Concurrent;
using System.Diagnostics;
using System.Net;
using UiPath.Rdp;
using UiPath.SessionTools;

namespace UiPath.FreeRdp.Tests.Faking;

public class UserExistsDetail
{
    private readonly NetworkCredential _credentials = new();

    public string UserName { get => _credentials.GetFullUserName(); set => _credentials.SetFullUserName(value); }
    public string Password { get => _credentials.Password; set => _credentials.Password = value; }

    internal string GetLocalUserName() => UserName.ToLowerInvariant()
        .Replace(".\\", "")
        .Replace(UserNames.DefaultDomainName.ToLowerInvariant() + "\\", "");

    public List<string> Groups { get; } = new() { "Remote Desktop Users", "Administrators" };

    public RdpConnectionSettings ToRdpConnectionSettings()
    => new(
        username: UserName.Split("\\")[1],
        password: Password,
        domain: UserName.Split("\\")[0]);
}

public interface IUserContext
{
    Task EnsureUserExists(UserExistsDetail userDetail);
}

public class UserContextReal : UserContextBase
{
    private readonly ILogger<IUserContext> _log;

    public UserContextReal(ILogger<IUserContext> log)
    {
        _log = log;
    }

    protected override async Task DoCreateUser(UserExistsDetail userDetail)
    {
        using var cts = new CancellationTokenSource(GlobalSettings.DefaultTimeout);

        await new ProcessRunner(_log).EnsureUserIsSetUp(userDetail.GetLocalUserName(), userDetail.Password, admin: true, cts.Token);
    }

}
public abstract class UserContextBase : IUserContext
{
    protected static readonly ConcurrentDictionary<string, string> CreatedUsersByName = new();

    public async Task EnsureUserExists(UserExistsDetail userDetail)
    {
        var userDetailAsJson = JsonConvert.SerializeObject(userDetail);
        _ = CreatedUsersByName.TryGetValue(userDetail.UserName, out var user);
        if (user == userDetailAsJson)
        {
            return;
        }

        await DoCreateUser(userDetail);
        _ = CreatedUsersByName.TryAdd(userDetail.UserName, userDetailAsJson);
    }

    public UserExistsDetail? GetUser(string userName)
    {
        if (CreatedUsersByName.TryGetValue(userName, out var user))
        {
            return JsonConvert.DeserializeObject<UserExistsDetail>(user);
        }

        return null;
    }

    protected abstract Task DoCreateUser(UserExistsDetail userDetail);
}

public class UserContextFake : UserContextBase
{
    protected override Task<bool> DoCreateUser(UserExistsDetail userDetail)
    => Task.FromResult(false);
}