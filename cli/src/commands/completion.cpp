#include "completion.hpp"

#include <iostream>
#include <string>

#include "../color.hpp"

// Every script below is hand-written against the actual flags each subcommand
// parses (see build.cpp/setup.cpp/config.cpp/start.cpp/update.cpp/clean.cpp's own
// printUsage() text) - there's no shared machine-readable flag table this generates
// from, so if a subcommand's flags change, update the matching script here too.
// Custom raw-string delimiters (BASH/ZSH/FISH/PWSH) rather than the default `)"` -
// several of these scripts contain that exact two-character sequence themselves.

namespace ebl::commands {

namespace {

void printUsage() {
  std::cout << R"(ebl completion <bash|zsh|fish|powershell>

Prints a shell completion script to stdout - completes subcommands, their flags, and
(for --artifact/--engine) the small set of valid values for those. Nothing else is
printed, so this is meant to be sourced/redirected directly:

  Bash         echo 'source <(ebl completion bash)' >> ~/.bashrc
  Zsh          echo 'source <(ebl completion zsh)' >> ~/.zshrc
  Fish         ebl completion fish > ~/.config/fish/completions/ebl.fish
  PowerShell   ebl completion powershell >> $PROFILE

Options:
  -h, --help   Show this help
)";
}

const char* bashScript() {
  return R"BASH(# ebl bash completion - see: ebl completion -h
_ebl_completions() {
  local cur prev
  cur="${COMP_WORDS[COMP_CWORD]}"
  prev="${COMP_WORDS[COMP_CWORD-1]}"

  local commands="build setup config start stop update clean completion"
  local global_opts="-h --help -v --version --about"

  if [[ ${COMP_CWORD} -eq 1 ]]; then
    COMPREPLY=($(compgen -W "${commands} ${global_opts}" -- "${cur}"))
    return
  fi

  local subcommand="${COMP_WORDS[1]}"
  case "${subcommand}" in
    build)
      case "${prev}" in
        -a|--artifact) COMPREPLY=($(compgen -W "apk aab" -- "${cur}")); return ;;
        -e|--engine) COMPREPLY=($(compgen -W "auto gradle eas" -- "${cur}")); return ;;
        --keystore|--docker-socket) COMPREPLY=($(compgen -f -- "${cur}")); return ;;
        -p|--profile|--store-password|--key-alias|--key-password|--expo-token|--runner-image|--gradle-cache-volume|--npm-cache-volume)
          return ;;
      esac
      if [[ "${cur}" == -* ]]; then
        COMPREPLY=($(compgen -W "--prod -a --artifact -p --profile -e --engine --release --keystore --store-password --key-alias --key-password --expo-token --runner-image --gradle-cache-volume --npm-cache-volume --docker-socket --json --status -h --help" -- "${cur}"))
      else
        COMPREPLY=($(compgen -d -- "${cur}"))
      fi
      ;;
    setup|config|start|stop)
      COMPREPLY=($(compgen -W "-h --help" -- "${cur}"))
      ;;
    update)
      case "${prev}" in
        --runner-image|--orchestrator-image|--web-image|--docker-socket) return ;;
      esac
      COMPREPLY=($(compgen -W "--runner-image --orchestrator-image --web-image --docker-socket -h --help" -- "${cur}"))
      ;;
    clean)
      case "${prev}" in
        --gradle-cache-volume|--npm-cache-volume|--docker-socket) return ;;
      esac
      COMPREPLY=($(compgen -W "--all --gradle-cache-volume --npm-cache-volume --docker-socket -h --help" -- "${cur}"))
      ;;
    completion)
      COMPREPLY=($(compgen -W "bash zsh fish powershell" -- "${cur}"))
      ;;
  esac
}
complete -F _ebl_completions ebl
)BASH";
}

const char* zshScript() {
  return R"ZSH(#compdef ebl
# ebl zsh completion - see: ebl completion -h

_ebl() {
  local -a commands
  commands=(
    'build:Build a project into a signed APK/AAB'
    'setup:One-time - check/install Docker, pull images'
    'config:Interactive wizard - projects folder, Expo token, ports'
    'start:Run the orchestrator + web GUI'
    'stop:Stop the orchestrator + web GUI'
    'update:Force-refresh the runner/orchestrator/web images'
    'clean:Remove stopped build containers'
    'completion:Print a shell completion script'
  )

  if (( CURRENT == 2 )); then
    _describe 'command' commands
    return
  fi

  local subcommand="${words[2]}"

  case "${subcommand}" in
    build)
      _arguments \
        '--prod[shortcut for --artifact aab --profile production]' \
        '(-a --artifact)'{-a,--artifact}'[apk or aab]:type:(apk aab)' \
        '(-p --profile)'{-p,--profile}'[eas.json build profile]:profile:' \
        '(-e --engine)'{-e,--engine}'[build engine]:engine:(auto gradle eas)' \
        '--release[sign with a real keystore instead of the debug keystore]' \
        '--keystore[path to a .jks/.keystore file]:file:_files' \
        '--store-password[keystore password]:password:' \
        '--key-alias[key alias]:alias:' \
        '--key-password[key password]:password:' \
        '--expo-token[Expo access token]:token:' \
        '--runner-image[runner image tag]:tag:' \
        '--gradle-cache-volume[Docker volume for the Gradle cache]:volume:' \
        '--npm-cache-volume[Docker volume for the npm cache]:volume:' \
        '--docker-socket[Docker socket path]:path:_files' \
        '--json[print the final result as JSON on stdout instead of the colored summary]' \
        '--status[live redrawing dashboard - phase/progress/elapsed/CPU/memory]' \
        '(-h --help)'{-h,--help}'[show help]' \
        '1:project path:_files -/'
      ;;
    setup|config|start|stop)
      _arguments '(-h --help)'{-h,--help}'[show help]'
      ;;
    update)
      _arguments \
        '--runner-image[runner image tag]:tag:' \
        '--orchestrator-image[orchestrator image tag]:tag:' \
        '--web-image[web image tag]:tag:' \
        '--docker-socket[Docker socket path]:path:_files' \
        '(-h --help)'{-h,--help}'[show help]'
      ;;
    clean)
      _arguments \
        '--all[also remove cache volumes and pulled images]' \
        '--gradle-cache-volume[Gradle cache volume name]:volume:' \
        '--npm-cache-volume[npm cache volume name]:volume:' \
        '--docker-socket[Docker socket path]:path:_files' \
        '(-h --help)'{-h,--help}'[show help]'
      ;;
    completion)
      _values 'shell' bash zsh fish powershell
      ;;
  esac
}

_ebl "$@"
)ZSH";
}

const char* fishScript() {
  return R"FISH(# ebl fish completion - see: ebl completion -h

set -l ebl_commands build setup config start stop update clean completion

complete -c ebl -n "not __fish_seen_subcommand_from $ebl_commands" -a build -d "Build a project into a signed APK/AAB"
complete -c ebl -n "not __fish_seen_subcommand_from $ebl_commands" -a setup -d "One-time - check/install Docker, pull images"
complete -c ebl -n "not __fish_seen_subcommand_from $ebl_commands" -a config -d "Interactive wizard - projects folder, Expo token, ports"
complete -c ebl -n "not __fish_seen_subcommand_from $ebl_commands" -a start -d "Run the orchestrator + web GUI"
complete -c ebl -n "not __fish_seen_subcommand_from $ebl_commands" -a stop -d "Stop the orchestrator + web GUI"
complete -c ebl -n "not __fish_seen_subcommand_from $ebl_commands" -a update -d "Force-refresh the runner/orchestrator/web images"
complete -c ebl -n "not __fish_seen_subcommand_from $ebl_commands" -a clean -d "Remove stopped build containers"
complete -c ebl -n "not __fish_seen_subcommand_from $ebl_commands" -a completion -d "Print a shell completion script"
complete -c ebl -n "not __fish_seen_subcommand_from $ebl_commands" -s h -l help -d "Show help"
complete -c ebl -n "not __fish_seen_subcommand_from $ebl_commands" -s v -l version -d "Show version"
complete -c ebl -n "not __fish_seen_subcommand_from $ebl_commands" -l about -d "Show project/developer/license/repository info"

complete -c ebl -n "__fish_seen_subcommand_from build" -l prod -d "Shortcut for --artifact aab --profile production"
complete -c ebl -n "__fish_seen_subcommand_from build" -s a -l artifact -xa "apk aab" -d "Artifact type"
complete -c ebl -n "__fish_seen_subcommand_from build" -s p -l profile -d "eas.json build profile"
complete -c ebl -n "__fish_seen_subcommand_from build" -s e -l engine -xa "auto gradle eas" -d "Build engine"
complete -c ebl -n "__fish_seen_subcommand_from build" -l release -d "Sign with a real keystore"
complete -c ebl -n "__fish_seen_subcommand_from build" -l keystore -r -F -d "Path to .jks/.keystore file"
complete -c ebl -n "__fish_seen_subcommand_from build" -l store-password -d "Keystore password"
complete -c ebl -n "__fish_seen_subcommand_from build" -l key-alias -d "Key alias"
complete -c ebl -n "__fish_seen_subcommand_from build" -l key-password -d "Key password"
complete -c ebl -n "__fish_seen_subcommand_from build" -l expo-token -d "Expo access token"
complete -c ebl -n "__fish_seen_subcommand_from build" -l runner-image -d "Runner image tag"
complete -c ebl -n "__fish_seen_subcommand_from build" -l gradle-cache-volume -d "Docker volume for the Gradle cache"
complete -c ebl -n "__fish_seen_subcommand_from build" -l npm-cache-volume -d "Docker volume for the npm cache"
complete -c ebl -n "__fish_seen_subcommand_from build" -l docker-socket -r -F -d "Docker socket path"
complete -c ebl -n "__fish_seen_subcommand_from build" -l json -d "Print the final result as JSON on stdout"
complete -c ebl -n "__fish_seen_subcommand_from build" -l status -d "Live redrawing dashboard - phase/progress/elapsed/CPU/memory"
complete -c ebl -n "__fish_seen_subcommand_from build" -s h -l help -d "Show help"

complete -c ebl -n "__fish_seen_subcommand_from setup config start stop" -s h -l help -d "Show help"

complete -c ebl -n "__fish_seen_subcommand_from update" -l runner-image -d "Runner image tag"
complete -c ebl -n "__fish_seen_subcommand_from update" -l orchestrator-image -d "Orchestrator image tag"
complete -c ebl -n "__fish_seen_subcommand_from update" -l web-image -d "Web image tag"
complete -c ebl -n "__fish_seen_subcommand_from update" -l docker-socket -r -F -d "Docker socket path"
complete -c ebl -n "__fish_seen_subcommand_from update" -s h -l help -d "Show help"

complete -c ebl -n "__fish_seen_subcommand_from clean" -l all -d "Also remove cache volumes and pulled images"
complete -c ebl -n "__fish_seen_subcommand_from clean" -l gradle-cache-volume -d "Gradle cache volume name"
complete -c ebl -n "__fish_seen_subcommand_from clean" -l npm-cache-volume -d "npm cache volume name"
complete -c ebl -n "__fish_seen_subcommand_from clean" -l docker-socket -r -F -d "Docker socket path"
complete -c ebl -n "__fish_seen_subcommand_from clean" -s h -l help -d "Show help"

complete -c ebl -n "__fish_seen_subcommand_from completion" -xa "bash zsh fish powershell"
)FISH";
}

const char* powershellScript() {
  return R"PWSH(# ebl PowerShell completion - see: ebl completion -h
Register-ArgumentCompleter -Native -CommandName ebl -ScriptBlock {
    param($wordToComplete, $commandAst, $cursorPosition)

    $commands = @('build', 'setup', 'config', 'start', 'stop', 'update', 'clean', 'completion')
    $globalOpts = @('-h', '--help', '-v', '--version', '--about')

    $tokens = $commandAst.CommandElements | ForEach-Object { $_.Extent.Text }

    if ($tokens.Count -le 1) {
        $candidates = $commands + $globalOpts
    } else {
        $subcommand = $tokens[1]
        $prev = $tokens[$tokens.Count - 1]
        switch ($subcommand) {
            'build' {
                if ($prev -eq '-a' -or $prev -eq '--artifact') {
                    $candidates = @('apk', 'aab')
                } elseif ($prev -eq '-e' -or $prev -eq '--engine') {
                    $candidates = @('auto', 'gradle', 'eas')
                } else {
                    $candidates = @('--prod', '-a', '--artifact', '-p', '--profile', '-e', '--engine', '--release', '--keystore', '--store-password', '--key-alias', '--key-password', '--expo-token', '--runner-image', '--gradle-cache-volume', '--npm-cache-volume', '--docker-socket', '--json', '--status', '-h', '--help')
                }
            }
            { $_ -in @('setup', 'config', 'start', 'stop') } {
                $candidates = @('-h', '--help')
            }
            'update' {
                $candidates = @('--runner-image', '--orchestrator-image', '--web-image', '--docker-socket', '-h', '--help')
            }
            'clean' {
                $candidates = @('--all', '--gradle-cache-volume', '--npm-cache-volume', '--docker-socket', '-h', '--help')
            }
            'completion' {
                $candidates = @('bash', 'zsh', 'fish', 'powershell')
            }
            default {
                $candidates = @()
            }
        }
    }

    $candidates | Where-Object { $_ -like "$wordToComplete*" } | ForEach-Object {
        [System.Management.Automation.CompletionResult]::new($_, $_, 'ParameterValue', $_)
    }
}
)PWSH";
}

}  // namespace

void printCompletionUsage() { printUsage(); }

int runCompletion(int argc, char** argv) {
  if (argc < 1) {
    std::cerr << ebl::color::red("Missing shell name - see: ebl completion -h") << "\n";
    return 2;
  }
  std::string shell = argv[0];
  if (shell == "-h" || shell == "--help") {
    printUsage();
    return 0;
  }
  if (shell == "bash") {
    std::cout << bashScript();
  } else if (shell == "zsh") {
    std::cout << zshScript();
  } else if (shell == "fish") {
    std::cout << fishScript();
  } else if (shell == "powershell" || shell == "pwsh") {
    std::cout << powershellScript();
  } else {
    std::cerr << ebl::color::red("Unknown shell \"" + shell + "\" - expected bash, zsh, fish, or powershell") << "\n";
    return 2;
  }
  return 0;
}

}  // namespace ebl::commands
