#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Generate worker-only Core wrappers with a small set of temporary GUI exceptions.

By default, validate the pinned interfaces and exceptions. CMake uses --output
to generate wrappers in the build directory and --check to verify them.
The parser deliberately only supports the declarations in the pinned interfaces;
an unsupported declaration is an error, never an uninstrumented fallback.
"""

import argparse
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parent.parent

# Temporary local allowances before runtime setup finishes. Onboarding can still
# be visible here: #980 moves that initialization to a worker, and #981 moves
# settings reads. Subscriptions still happen while constructing NodeModel.
# Delete each allowance as its caller moves off the GUI thread.
STARTUP_EXCEPTIONS = {
    "Node::baseInitialize": "bool baseInitialize()",
    "Node::getPersistentSetting": "common::SettingsValue getPersistentSetting(const std::string& name)",
    "Node::handleMessageBox": "std::unique_ptr<Handler> handleMessageBox(MessageBoxFn fn)",
    "Node::handleQuestion": "std::unique_ptr<Handler> handleQuestion(QuestionFn fn)",
    "Node::handleNotifyNumConnectionsChanged": "std::unique_ptr<Handler> handleNotifyNumConnectionsChanged(NotifyNumConnectionsChangedFn fn)",
    "Node::handleNotifyNetworkActiveChanged": "std::unique_ptr<Handler> handleNotifyNetworkActiveChanged(NotifyNetworkActiveChangedFn fn)",
    "Node::handleNotifyAlertChanged": "std::unique_ptr<Handler> handleNotifyAlertChanged(NotifyAlertChangedFn fn)",
    "Node::handleBannedListChanged": "std::unique_ptr<Handler> handleBannedListChanged(BannedListChangedFn fn)",
    "Node::handleNotifyBlockTip": "std::unique_ptr<Handler> handleNotifyBlockTip(NotifyBlockTipFn fn)",
    "Node::handleNotifyHeaderTip": "std::unique_ptr<Handler> handleNotifyHeaderTip(NotifyHeaderTipFn fn)",
}

# Transitional allowance for #939's GUI shutdown polling. Remove with #981.
TEMPORARY_GUI_EXCEPTIONS = {
    "Node::shutdownRequested": "bool shutdownRequested()",
}

# Ownership handling is separate from GUI-thread permission. These methods
# return checked objects instead of exposing backend handles directly.
# Changed signatures must be reviewed against the custom forwarding bodies.
HANDLE_METHODS = {
    "Node::listExternalSigners": "std::vector<std::unique_ptr<ExternalSigner>> listExternalSigners()",
    "Node::walletLoader": "WalletLoader& walletLoader()",
    "WalletLoader::createWallet": "util::Result<std::unique_ptr<Wallet>> createWallet(const std::string& name, const SecureString& passphrase, uint64_t wallet_creation_flags, std::vector<bilingual_str>& warnings)",
    "WalletLoader::loadWallet": "util::Result<std::unique_ptr<Wallet>> loadWallet(const std::string& name, std::vector<bilingual_str>& warnings)",
    "WalletLoader::restoreWallet": "util::Result<std::unique_ptr<Wallet>> restoreWallet(const fs::path& backup_file, const std::string& wallet_name, std::vector<bilingual_str>& warnings, bool load_after_restore)",
    "WalletLoader::migrateWallet": "util::Result<WalletMigrationResult> migrateWallet(const std::string& name, const SecureString& passphrase)",
    "WalletLoader::getWallets": "std::vector<std::unique_ptr<Wallet>> getWallets()",
    "WalletLoader::handleLoadWallet": "std::unique_ptr<Handler> handleLoadWallet(LoadWalletFn fn)",
}

# Denied on every thread, regardless of signature, because they bypass wrappers.
FORBIDDEN_METHODS = {"Node::context", "Node::setContext", "Chain::context", "Wallet::wallet", "WalletLoader::context"}

# Reviewed value types contain data, not backend interface handles. Container
# elements are checked recursively; an unfamiliar result type needs review.
VALUE_TYPES = {
    "void", "bool", "int", "unsigned int", "int64_t", "size_t", "double", "std::string",
    "BCLog::CategoryMask", "CAmount", "CFeeRate", "CTransactionRef", "CTxDestination",
    "CoinsList", "Coin", "CNetAddr", "COutPoint", "LocalServiceInfo", "OutputType", "Proxy",
    "RBFTransactionState", "SigningResult", "UniValue", "WalletAddress", "WalletBalances",
    "WalletTx", "WalletTxOut", "bilingual_str", "uint256", "common::PSBTError",
    "common::SettingsValue", "node::TransactionError", "wallet::CreatedTransactionResult",
}


def read_headers():
    return {name: re.sub(r"//[^\n]*|/\*.*?\*/", "", (ROOT / f"bitcoin/src/interfaces/{name}.h").read_text(encoding="utf8"), flags=re.S)
            for name in ("node", "chain", "wallet")}


def backend_handle_types(headers):
    names = {"Node", "Chain", "Wallet", "WalletLoader", "ExternalSigner", "NodeContext", "WalletContext", "CWallet", "WalletMigrationResult"}
    aliases = [match.groups() for source in headers.values()
               for match in re.finditer(r"\busing\s+(\w+)\s*=\s*([^;]+);", source)]
    # Callback aliases can deliver handles even when the method returns Handler.
    while True:
        added = {name for name, definition in aliases if names.intersection(re.findall(r"\b\w+\b", definition))} - names
        if not added:
            return names
        names.update(added)


def is_value_type(type_name):
    if type_name in VALUE_TYPES or type_name == "std::unique_ptr<Handler>":
        return True
    container = re.fullmatch(r"(?:std::(?:vector|set|map|pair|tuple|optional)|util::Result)<(.+)>", type_name)
    return bool(container and all(is_value_type(item) for item in split_params(container[1])))


def method_policy(key, signature, handle_types):
    if key in FORBIDDEN_METHODS:
        return "Forbidden"
    expected = HANDLE_METHODS.get(key)
    if expected:
        if signature != expected:
            raise ValueError(f"Review changed handle forwarding for {key}: {signature}")
    else:
        return_type = signature[:signature.index(" " + key.split("::")[-1] + "(")]
        if not is_value_type(return_type) or handle_types.intersection(re.findall(r"\b\w+\b", signature)):
            raise ValueError(f"Review backend handle or result type for {key}: {signature}")
    for exceptions, policy in ((STARTUP_EXCEPTIONS, "Bootstrap"), (TEMPORARY_GUI_EXCEPTIONS, "LocalShutdown")):
        if key in exceptions:
            if signature != exceptions[key]:
                raise ValueError(f"Review changed GUI-thread exception for {key}: {signature}")
            return policy
    return "Worker"


def body(source, name):
    declaration = re.search(r"class " + name + r"(?:\s*:\s*public \w+)?\s*\{", source)
    if not declaration:
        raise ValueError(f"Missing interface declaration: {name}")
    start = declaration.end()
    depth = 1
    for pos in range(start, len(source)):
        depth += (source[pos] == "{") - (source[pos] == "}")
        if depth == 0:
            return source[start:pos]
    raise ValueError(f"Unclosed class: {name}")


def split_params(params):
    result = []
    depth = 0
    start = 0
    for pos, char in enumerate(params + ","):
        depth += (char in "<{(") - (char in ">})")
        if char == "," and depth == 0:
            value = params[start:pos].split("=", 1)[0].strip()
            if value:
                result.append(value)
            start = pos + 1
    return result


def declarations(name, headers):
    header = "wallet" if name in ("Wallet", "WalletLoader") else "chain" if name in ("Chain", "ChainClient") else "node"
    source = body(headers[header], name)
    if name == "Chain":
        nested = body(source, "Notifications")
        source = source.replace(nested, "")
    matches = list(re.finditer(r"virtual\s+([^;{}]+?\([^;{}]*(?:\{\}[^;{}]*)*\))\s*(?:=\s*0\s*;|\{[^{}]*\})", source))
    # Destructors have no backend operation. Every other virtual must be parsed.
    count = len(re.findall(r"\bvirtual\b", source)) - len(re.findall(r"virtual\s+~", source))
    matches = [m for m in matches if not m[1].startswith("~")]
    if len(matches) != count:
        raise ValueError(f"Unsupported virtual declaration in {name}: parsed {len(matches)}/{count}")
    methods = []
    for match in matches:
        signature = " ".join(match[1].split())
        ret, method, params = re.fullmatch(r"(.+?)\s+(\w+)\((.*)\)", signature).groups()
        params = split_params(params)
        signature = f"{ret} {method}({', '.join(params)})"
        args = []
        for param in params:
            arg = re.search(r"(\w+)$", param)[1]
            args.append(arg if "&" in param and "&&" not in param else f"std::move({arg})")
        methods.append((method, signature, ", ".join(args)))
    if name == "WalletLoader":
        methods = declarations("ChainClient", headers) + methods
    return methods


def generate():
    headers = read_headers()
    handle_types = backend_handle_types(headers)
    lines = ["// Generated by test/generate_thread_audit.py. Do not edit.",
             "// Worker-only by default; temporary GUI exceptions are reviewed in the generator.", "using namespace interfaces;", ""]
    seen = set()
    for name in ("ExternalSigner", "Wallet", "WalletLoader", "Chain", "Node"):
        lines += [f"class Checked{name} final : public {name}", "{", "public:",
                  f"    Checked{name}(std::unique_ptr<{name}> backend, std::shared_ptr<ThreadAudit> audit)",
                  "        : m_owner{std::move(backend)}, m_backend{m_owner.get()}, m_audit{std::move(audit)} {}"]
        if name == "WalletLoader":
            lines += ["    CheckedWalletLoader(WalletLoader& backend, std::shared_ptr<ThreadAudit> audit)",
                      "        : m_backend{&backend}, m_audit{std::move(audit)} {}"]
        for method, signature, args in declarations(name, headers):
            key = f"{name}::{method}"
            if key in seen:
                raise ValueError(f"Review overloaded method: {key}")
            seen.add(key)
            policy = method_policy(key, signature, handle_types)
            check = f'        m_audit->check("{key}", ThreadPolicy::{policy});'
            call = f"m_backend->{method}({args})"
            code = [f"        return {call};"]
            if key == "Node::walletLoader":
                code = ["        std::lock_guard lock{m_loader_mutex};",
                        "        if (!m_loader) m_loader = std::make_unique<CheckedWalletLoader>(m_backend->walletLoader(), m_audit);",
                        "        return *m_loader;"]
            elif name == "WalletLoader" and method in ("createWallet", "loadWallet", "restoreWallet"):
                code = [f"        auto result = {call};", "        if (result) *result = CheckWallet(std::move(*result), m_audit);", "        return result;"]
            elif key == "WalletLoader::migrateWallet":
                code = [f"        auto result = {call};", "        if (result) result->wallet = CheckWallet(std::move(result->wallet), m_audit);", "        return result;"]
            elif key == "WalletLoader::getWallets":
                code = [f"        auto wallets = {call};", "        for (auto& wallet : wallets) wallet = CheckWallet(std::move(wallet), m_audit);", "        return wallets;"]
            elif key == "WalletLoader::handleLoadWallet":
                code = ["        return m_backend->handleLoadWallet([audit = m_audit, fn = std::move(fn)](std::unique_ptr<Wallet> wallet) {",
                        "            fn(CheckWallet(std::move(wallet), audit));", "        });"]
            elif key == "Node::listExternalSigners":
                # A signer only exposes its display name. Keep even that call
                # audited; future interface additions are checked below too.
                code = [f"        auto signers = {call};",
                        "        for (auto& signer : signers) signer = std::make_unique<CheckedExternalSigner>(std::move(signer), m_audit);",
                        "        return signers;"]
            lines += [f"    {signature} override", "    {", check, *code, "    }"]
        lines += ["private:", f"    std::unique_ptr<{name}> m_owner;", f"    {name}* m_backend;", "    std::shared_ptr<ThreadAudit> m_audit;"]
        if name == "Node":
            lines += ["    std::mutex m_loader_mutex;", "    std::unique_ptr<CheckedWalletLoader> m_loader;"]
        lines += ["};", ""]
    stale = (STARTUP_EXCEPTIONS.keys() | TEMPORARY_GUI_EXCEPTIONS.keys() | HANDLE_METHODS.keys() | FORBIDDEN_METHODS) - seen
    if stale:
        raise ValueError(f"Remove stale exceptions or forwarding rules: {sorted(stale)}")
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--output", type=Path, help="write generated wrappers to this build-directory path")
    mode.add_argument("--check", type=Path, help="check existing build-directory wrappers against the policy")
    args = parser.parse_args()
    generated = generate()
    if args.output:
        args.output.write_text(generated, encoding="utf8")
    elif args.check and args.check.read_text(encoding="utf8") != generated:
        raise SystemExit("Thread wrappers out of date. Rebuild the thread-audit test targets.")


if __name__ == "__main__":
    main()
