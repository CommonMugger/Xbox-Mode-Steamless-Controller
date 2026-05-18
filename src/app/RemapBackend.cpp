#include "RemapBackend.h"
#include "SteamLibrary.h"
#include "logging/Log.h"
#include <algorithm>

void RemapBackend::Load(const PaddleMappings& defaultMappings, const PaddleActionBindings& defaultActions) {
    PaddleConfig::EnsureExists();
    m_profiles = PaddleConfig::LoadProfiles(defaultMappings, defaultActions);
    m_activeProfileId = L"default";
}

const std::wstring& RemapBackend::GetActiveProfileId() const {
    return m_activeProfileId;
}

const RemapProfile* RemapBackend::GetActiveProfile() const {
    return FindProfileById(m_activeProfileId);
}

const RemapProfile* RemapBackend::GetProfileById(const std::wstring& profileId) const {
    return FindProfileById(profileId);
}

PaddleMappings RemapBackend::GetActiveMappings() const {
    const RemapProfile* profile = GetActiveProfile();
    return profile ? profile->mappings : PaddleMappings{};
}

PaddleActionBindings RemapBackend::GetActiveActions() const {
    const RemapProfile* profile = GetActiveProfile();
    return profile ? profile->actions : PaddleActionBindings{};
}

RemapProfile* RemapBackend::FindProfileById(const std::wstring& profileId) {
    const std::wstring normalized = PaddleConfig::NormalizeProfileId(profileId);
    for (RemapProfile& profile : m_profiles) {
        if (profile.id == normalized)
            return &profile;
    }
    return nullptr;
}

const RemapProfile* RemapBackend::FindProfileById(const std::wstring& profileId) const {
    return const_cast<RemapBackend*>(this)->FindProfileById(profileId);
}

RemapProfile* RemapBackend::EnsureProfileExists(const std::wstring& profileId, const std::wstring& baseProfileId) {
    const std::wstring normalizedId = PaddleConfig::NormalizeProfileId(profileId);
    if (normalizedId.empty())
        return nullptr;

    if (RemapProfile* existing = FindProfileById(normalizedId))
        return existing;

    const std::wstring normalizedBaseId = PaddleConfig::NormalizeProfileId(baseProfileId);
    const RemapProfile* base = FindProfileById(normalizedBaseId);
    if (!base)
        base = FindProfileById(L"default");

    RemapProfile profile = base ? *base : RemapProfile{};
    profile.id = normalizedId;
    m_profiles.push_back(std::move(profile));
    PersistProfiles();
    logging::Logf("[Profiles] Created profile id=%s base=%s",
                  logging::Narrow(normalizedId).c_str(),
                  logging::Narrow(base ? base->id : std::wstring(L"default")).c_str());
    return &m_profiles.back();
}

bool RemapBackend::SetActiveProfileId(const std::wstring& profileId, bool force) {
    const std::wstring normalizedId = PaddleConfig::NormalizeProfileId(profileId);
    if (!force && normalizedId == m_activeProfileId)
        return false;

    if (!EnsureProfileExists(normalizedId))
        return false;

    m_activeProfileId = normalizedId;
    return true;
}

void RemapBackend::SaveActiveProfile(const PaddleMappings& mappings, const PaddleActionBindings& actions) {
    RemapProfile* profile = FindProfileById(m_activeProfileId);
    if (!profile) {
        m_profiles.push_back(RemapProfile{ m_activeProfileId, mappings, actions });
        profile = &m_profiles.back();
    } else {
        profile->mappings = mappings;
        profile->actions = actions;
    }
    PersistProfiles();
}

bool RemapBackend::DeleteProfile(const std::wstring& profileId) {
    const std::wstring normalizedId = PaddleConfig::NormalizeProfileId(profileId);
    if (normalizedId == L"default")
        return false;

    const auto before = m_profiles.size();
    m_profiles.erase(std::remove_if(m_profiles.begin(), m_profiles.end(),
        [&](const RemapProfile& profile) { return profile.id == normalizedId; }),
        m_profiles.end());
    if (m_profiles.size() == before)
        return false;

    if (m_activeProfileId == normalizedId)
        m_activeProfileId = L"default";
    PersistProfiles();
    return true;
}

void RemapBackend::PersistProfiles() const {
    PaddleConfig::SaveProfiles(m_profiles);
}

std::vector<std::wstring> RemapBackend::GetInstalledGames() const {
    return SteamLibrary::ListInstalledGameNames();
}

std::vector<std::wstring> RemapBackend::RefreshInstalledGames() const {
    return SteamLibrary::RefreshInstalledGameNames();
}

std::vector<std::wstring> RemapBackend::GetGameSourceSpecs() const {
    return SteamLibrary::GetConfiguredSourceSpecs();
}

void RemapBackend::SetGameSourceSpecs(const std::vector<std::wstring>& specs) {
    SteamLibrary::SetConfiguredSourceSpecs(specs);
}

std::wstring RemapBackend::MatchProcessListToInstalledGame(const std::vector<std::wstring>& processPaths) const {
    return SteamLibrary::MatchProcessListToInstalledGame(processPaths);
}
