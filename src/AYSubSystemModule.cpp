#include <AYGameLoop/SubSystemModule.h>

#include <utility>

namespace ayt::game
{

namespace
{

[[nodiscard]] ayt::module::ModuleResult installationFailure(
    const std::string& message)
{
    return ayt::module::ModuleResult::failure(
        ayt::module::ModuleErrorCode::InstallationFailed,
        message);
}

} // namespace

SubSystemModule::SubSystemModule(
    ayt::module::ModuleDescriptor descriptor,
    std::string subSystemName,
    Factory factory,
    InstallHook installHook,
    ExistingSubSystemPolicy existingPolicy)
    : _descriptor(std::move(descriptor)),
      _subSystemName(std::move(subSystemName)),
      _factory(std::move(factory)),
      _installHook(std::move(installHook)),
      _existingPolicy(existingPolicy)
{
}

SubSystemModule::SubSystemModule(
    ayt::module::ModuleDescriptor descriptor,
    std::string subSystemName,
    ContextFactory factory,
    InstallHook installHook,
    ExistingSubSystemPolicy existingPolicy)
    : _descriptor(std::move(descriptor)),
      _subSystemName(std::move(subSystemName)),
      _contextFactory(std::move(factory)),
      _installHook(std::move(installHook)),
      _existingPolicy(existingPolicy)
{
}

const ayt::module::ModuleDescriptor& SubSystemModule::descriptor()
    const noexcept
{
    return _descriptor;
}

ayt::module::ModuleResult SubSystemModule::install(
    ayt::module::IModuleContext& context)
{
    auto* service = context.findServiceAs<ISubSystemModuleService>(
        kSubSystemModuleService);
    if (service == nullptr) {
        return installationFailure(
            "SubSystemModuleService is unavailable for '" +
            _descriptor.id + "'");
    }
    if (_subSystemName.empty() || (!_factory && !_contextFactory)) {
        return installationFailure(
            "Module '" + _descriptor.id +
            "' has no valid subsystem factory");
    }

    _installedSubSystem = service->findSubSystem(_subSystemName);
    _ownsRegistration = false;

    if (_installedSubSystem != nullptr
        && _existingPolicy == ExistingSubSystemPolicy::Reject) {
        _installedSubSystem = nullptr;
        return installationFailure(
            "Subsystem '" + _subSystemName
            + "' was registered before module '" + _descriptor.id
            + "'; explicitly select ExistingSubSystemPolicy::Adopt "
              "only when the existing instance and configuration are "
              "compatible");
    }

    if (_installedSubSystem == nullptr) {
        std::unique_ptr<ISubSystem> system = _contextFactory
            ? _contextFactory(context)
            : _factory();
        if (!system) {
            return installationFailure(
                "Module '" + _descriptor.id +
                "' failed to create subsystem '" + _subSystemName + "'");
        }
        const char* actualName = system->getName();
        if (actualName == nullptr || _subSystemName != actualName) {
            return installationFailure(
                "Module '" + _descriptor.id +
                "' created a subsystem with an unexpected name");
        }

        ISubSystem* expected = system.get();
        if (!service->installSubSystem(std::move(system))) {
            return installationFailure(
                "Module '" + _descriptor.id +
                "' could not register subsystem '" + _subSystemName + "'");
        }

        _installedSubSystem = service->findSubSystem(_subSystemName);
        if (_installedSubSystem != expected) {
            _installedSubSystem = nullptr;
            return installationFailure(
                "Subsystem '" + _subSystemName +
                "' was not published synchronously during module startup");
        }
        _ownsRegistration = true;
    }

    if (_installHook) {
        return _installHook(context, *_installedSubSystem);
    }
    return ayt::module::ModuleResult::success();
}

void SubSystemModule::shutdown(
    ayt::module::IModuleContext& context) noexcept
{
    if (_ownsRegistration && _installedSubSystem != nullptr) {
        if (auto* service = context.findServiceAs<ISubSystemModuleService>(
                kSubSystemModuleService)) {
            service->uninstallSubSystem(
                _subSystemName,
                _installedSubSystem);
        }
    }
    _installedSubSystem = nullptr;
    _ownsRegistration = false;
}

ISubSystem* SubSystemModule::installedSubSystem() const noexcept
{
    return _installedSubSystem;
}

bool SubSystemModule::ownsRegistration() const noexcept
{
    return _ownsRegistration;
}

} // namespace ayt::game
