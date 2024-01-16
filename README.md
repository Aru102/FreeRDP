## UiPath fork of FreeRDP

This repo forks the FreeRDP repo, thus allowing us to make changes that fit our needs and augment the codebase with other components.
From time to time there is a need to merge the changes from the original repo into this one.

### ❗When updating the FreeRDP library from the official repo, please update the following in this file (README.md):

|**Original repository tag/branch used:**| `2.5.0` |
| --- | --- |
|**Original corresponding commit hash:**| `d50aef95520df4216c638495a6049125c00742cb` |

## Update Guide

- consider the new Base Git Tag from the official repo onto which we'll rebase our changes
  
  > **NOTE** For simplicity, let's consider `2.11.2` is the tag we want to rebase onto.
  
- sync our fork to include that Git Tag. On local, after cloning UiPath/FreeRdp, execute the following:

  ```pwsh
  git fetch --tags --all   # this will fetch all tags from all upstreams, including from FreeRdp/FreeRdp
  git push --tags          # this will push the new tags to UiPath/FreeRdp
  ```
- checkout the `uipath` branch
- ‼️ create a new branch to backup the current state (i.e. `support/before_update_to_2_11_2`)
- create **and checkout** a new branch that will be used as a work branch (i.e. `feat/update_to_2_11_2`)
  
- identify the Commit Hash, which is the parent of our 1st customization Commit

  > **NOTE** At the time of writing, our 1st customization Commit's message's 1st line is `Add uipath changes from previous version (2.0.0-rc3)`

- Make sure you've checked out the work branch (i.e. `feat/update_to_2_11_2), and run **git rebase --onto**, so that all UiPath customization commits are replayed onto the new Base Git Tag:

  ```pwsh
  git rebase --onto <NEW BASE Git Tag> <Our 1st customization Commit's Hash> <The work branch>
  ```


### Build instructions
* Visual Studio 2022 installed in `C:\Program Files` required.  

#### 
* Install [StrawberryPerl](http://strawberryperl.com).  Make sure the `perl` command is in PATH.
  You may try on newer Windows 10:
```
winget install -e --id StrawberryPerl.StrawberryPerl
```

#### Build FreeRDP and Build OpenSSL (dependency for FreeRDP)

* Steps
** Clone [OpenSSL](https://github.com/openssl/openssl) to `..\openssl` && Checkout tag `OpenSSL_1_0_2u` (getOpenSsl)
** Generate OpenSSL build to `..\OpenSSL-VC-64`.  (buildOpenSsl)
** Use CMake to generate and then build Visual Studio 2022 solutions.  (BuildFreeRDP)
** The freerdp solution is generated in `.\Build\x64\` directories.

* Scripts
** Simple
```
cd .ci/Scripts
.\PrepareFreeRdpDev
```
** or detailed
```
cd .ci/Scripts
.\getOpenSsl
.\buildOpenSsl
.\buildFreeRDP Debug
```

### Work on the FreeRdpClient
* Open [UiPath.FreeRdpClient/UiPath.FreeRdpClient.sln](file://UiPath.FreeRdpClient/UiPath.FreeRdpClient.sln)
* To test with a nugetRef instead of projectRef edit the [UiPath.FreeRdp.Tests.csproj](file://UiPath.FreeRdpClient/UiPath.FreeRdpClient.Tests/UiPath.FreeRdp.Tests.csproj)
search for: `<When Condition="'$(UseNugetRef)'!=''">`
