#ifndef MGB64_ENV_OWNERSHIP_H
#define MGB64_ENV_OWNERSHIP_H

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace EnvOwnership {

struct EnvOp {
    std::string key;
    bool        set;    // true: setenv(key,value); false: unsetenv(key)
    std::string value;  // meaningful only when set == true
};

std::vector<EnvOp> hatchOps(bool shootOutLights, bool autoAim);

std::map<std::string, std::string> parseAdvanced(const std::string &text);

struct Reconciliation {
    std::vector<EnvOp> ops;     // apply in order
    std::string        record;  // new ownership record to persist
};

// One launcher-owned key's ownership state, persisted across a re-exec / relaunch.
//   hadExternal : an external value existed when the launcher first claimed the key
//   external    : that captured external value (to restore on removal)
//   applied     : the value the launcher last set the key to — a removed key is only
//                 unset/restored when the LIVE env still equals this, so a value the
//                 user changed externally since (e.g. a fresh relaunch inheriting a new
//                 shell value) is never clobbered.
struct EnvOwned {
    bool        hadExternal;
    std::string external;
    std::string applied;
};

Reconciliation reconcile(
    const std::map<std::string, std::string> &desired,
    const std::string &priorRecord,
    const std::function<std::optional<std::string>(const std::string &)> &lookupEnv);

std::string encodeRecord(const std::map<std::string, EnvOwned> &owned);
std::map<std::string, EnvOwned> decodeRecord(const std::string &rec);

}  // namespace EnvOwnership

#endif  // MGB64_ENV_OWNERSHIP_H
