#pragma once

#include <AYGameLoop/IGameLoop.h>
#include <AYModule/IModule.h>

#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace ayt::game
{

// Reserved AYModule service used by startup modules to publish GameLoop
// subsystems through the active engine host. The service owns no subsystem;
// ownership is transferred to GameLoop during install().
inline constexpr std::string_view kSubSystemModuleService =
    "ayt.game.SubSystemModuleService";

class ISubSystemModuleService
{
public:
    virtual ~ISubSystemModuleService() = default;

    [[nodiscard]] virtual ISubSystem* findSubSystem(
        std::string_view name) noexcept = 0;
    [[nodiscard]] virtual bool installSubSystem(
        std::unique_ptr<ISubSystem> system) = 0;
    virtual void uninstallSubSystem(
        std::string_view name,
        ISubSystem* expectedInstance) noexcept = 0;
};

enum class ExistingSubSystemPolicy
{
    Reject,
    Adopt
};

// Reusable startup adapter for an ISubSystem. The module creates and publishes
// the subsystem during install(); GameLoop remains responsible for its
// initialize/update/shutdown lifecycle. A pre-existing subsystem is rejected
// by default so a mixed legacy/module startup cannot silently accept the wrong
// instance. ExistingSubSystemPolicy::Adopt is an explicit compatibility escape
// hatch. Module shutdown only withdraws an instance that this adapter installed
// and is therefore safe after GameLoop has already cleared the registry.
class SubSystemModule : public ayt::module::IModule
{
public:
    using Factory = std::function<std::unique_ptr<ISubSystem>()>;
    using ContextFactory = std::function<std::unique_ptr<ISubSystem>(
        ayt::module::IModuleContext&)>;
    using InstallHook = std::function<ayt::module::ModuleResult(
        ayt::module::IModuleContext&,
        ISubSystem&)>;

    SubSystemModule(
        ayt::module::ModuleDescriptor descriptor,
        std::string subSystemName,
        Factory factory,
        InstallHook installHook = {},
        ExistingSubSystemPolicy existingPolicy =
            ExistingSubSystemPolicy::Reject);

    // Use this overload when construction depends on services or subsystems
    // installed by an earlier module. The context is supplied only during
    // install(), after module dependency ordering has been resolved.
    SubSystemModule(
        ayt::module::ModuleDescriptor descriptor,
        std::string subSystemName,
        ContextFactory factory,
        InstallHook installHook = {},
        ExistingSubSystemPolicy existingPolicy =
            ExistingSubSystemPolicy::Reject);

    [[nodiscard]] const ayt::module::ModuleDescriptor& descriptor()
        const noexcept override;
    [[nodiscard]] ayt::module::ModuleResult install(
        ayt::module::IModuleContext& context) override;
    void shutdown(ayt::module::IModuleContext& context) noexcept override;

    [[nodiscard]] ISubSystem* installedSubSystem() const noexcept;
    [[nodiscard]] bool ownsRegistration() const noexcept;

private:
    ayt::module::ModuleDescriptor _descriptor;
    std::string _subSystemName;
    Factory _factory;
    ContextFactory _contextFactory;
    InstallHook _installHook;
    ExistingSubSystemPolicy _existingPolicy =
        ExistingSubSystemPolicy::Reject;
    ISubSystem* _installedSubSystem = nullptr;
    bool _ownsRegistration = false;
};

} // namespace ayt::game
