#pragma once

#include "PaddleConfig.h"
#include <string>
#include <vector>

class RemapBackend {
public:
    void Load(const PaddleMappings& defaultMappings, const PaddleActionBindings& defaultActions);

    const std::wstring& GetActiveProfileId() const;
    const RemapProfile* GetActiveProfile() const;
    const RemapProfile* GetProfileById(const std::wstring& profileId) const;
    PaddleMappings GetActiveMappings() const;
    PaddleActionBindings GetActiveActions() const;

    RemapProfile* EnsureProfileExists(const std::wstring& profileId,
                                      const std::wstring& baseProfileId = L"default");
    bool SetActiveProfileId(const std::wstring& profileId, bool force = false);
    void SaveActiveProfile(const PaddleMappings& mappings, const PaddleActionBindings& actions);
    bool DeleteProfile(const std::wstring& profileId);
    void PersistProfiles() const;

    std::vector<std::wstring> GetInstalledGames() const;
    std::vector<std::wstring> RefreshInstalledGames() const;
    std::vector<std::wstring> GetGameSourceSpecs() const;
    void SetGameSourceSpecs(const std::vector<std::wstring>& specs);
    std::wstring MatchProcessListToInstalledGame(const std::vector<std::wstring>& processPaths) const;

private:
    RemapProfile* FindProfileById(const std::wstring& profileId);
    const RemapProfile* FindProfileById(const std::wstring& profileId) const;

    std::vector<RemapProfile> m_profiles;
    std::wstring m_activeProfileId = L"default";
};
