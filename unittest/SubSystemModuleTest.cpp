#include <AYGameLoop/SubSystemModule.h>
#include <AYModule/ModuleContext.h>
#include <AYTest.h>

#include <memory>
#include <string>

namespace ayt::game::test
{
namespace
{

class ProbeSubSystem final : public ISubSystem
{
public:
    const char* getName() const override { return "ModuleProbe"; }

    const SubSystemDescriptor& getDescriptor() const override
    {
        static const SubSystemDescriptor descriptor{
            .name = "ModuleProbe"};
        return descriptor;
    }

    bool initialize() override { return true; }
    void update(float) override {}
    void fixedUpdate(float) override {}
    void shutdown() override {}
};

class ProbeModuleService final : public ISubSystemModuleService
{
public:
    ISubSystem* findSubSystem(std::string_view name) noexcept override
    {
        return _system && name == _system->getName() ? _system.get() : nullptr;
    }

    bool installSubSystem(std::unique_ptr<ISubSystem> system) override
    {
        if (!system || _system) {
            return false;
        }
        _system = std::move(system);
        return true;
    }

    void uninstallSubSystem(
        std::string_view name,
        ISubSystem* expectedInstance) noexcept override
    {
        if (_system && name == _system->getName()
            && _system.get() == expectedInstance) {
            _system.reset();
        }
    }

private:
    std::unique_ptr<ISubSystem> _system;
};

ayt::module::ModuleDescriptor probeDescriptor()
{
    return {
        .id = "Test.SubSystemModule",
        .displayName = "Subsystem Module Probe",
        .version = "1.0.0",
        .dependencies = {}};
}

} // namespace

TEST_SUITE(SubSystemModuleTests)

TEST_CASE(installs_and_uninstalls_the_owned_subsystem)
{
    ProbeModuleService service;
    ayt::module::ModuleContext context;
    CHECK_TRUE(context.bindService(
        std::string(kSubSystemModuleService),
        &service));

    int factoryCalls = 0;
    SubSystemModule module(
        probeDescriptor(),
        "ModuleProbe",
        [&factoryCalls]() {
            ++factoryCalls;
            return std::make_unique<ProbeSubSystem>();
        });

    CHECK_TRUE(module.install(context).succeeded());
    CHECK(module.installedSubSystem() != nullptr);
    CHECK_TRUE(module.ownsRegistration());
    CHECK(factoryCalls == 1);
    CHECK(service.findSubSystem("ModuleProbe")
        == module.installedSubSystem());

    module.shutdown(context);
    CHECK(service.findSubSystem("ModuleProbe") == nullptr);
    CHECK(module.installedSubSystem() == nullptr);
    CHECK_FALSE(module.ownsRegistration());
}

TEST_CASE(rejects_a_preexisting_subsystem_by_default)
{
    ProbeModuleService service;
    CHECK_TRUE(service.installSubSystem(
        std::make_unique<ProbeSubSystem>()));
    ISubSystem* preexisting = service.findSubSystem("ModuleProbe");

    ayt::module::ModuleContext context;
    CHECK_TRUE(context.bindService(
        std::string(kSubSystemModuleService),
        &service));

    int factoryCalls = 0;
    SubSystemModule module(
        probeDescriptor(),
        "ModuleProbe",
        [&factoryCalls]() {
            ++factoryCalls;
            return std::make_unique<ProbeSubSystem>();
        });

    const auto result = module.install(context);
    CHECK_FALSE(result.succeeded());
    CHECK(result.code() == ayt::module::ModuleErrorCode::InstallationFailed);
    CHECK(module.installedSubSystem() == nullptr);
    CHECK_FALSE(module.ownsRegistration());
    CHECK(factoryCalls == 0);

    module.shutdown(context);
    CHECK(service.findSubSystem("ModuleProbe") == preexisting);
}

TEST_CASE(explicitly_adopts_but_does_not_remove_a_preexisting_subsystem)
{
    ProbeModuleService service;
    CHECK_TRUE(service.installSubSystem(
        std::make_unique<ProbeSubSystem>()));
    ISubSystem* preexisting = service.findSubSystem("ModuleProbe");

    ayt::module::ModuleContext context;
    CHECK_TRUE(context.bindService(
        std::string(kSubSystemModuleService),
        &service));

    int factoryCalls = 0;
    SubSystemModule module(
        probeDescriptor(),
        "ModuleProbe",
        [&factoryCalls]() {
            ++factoryCalls;
            return std::make_unique<ProbeSubSystem>();
        },
        {},
        ExistingSubSystemPolicy::Adopt);

    CHECK_TRUE(module.install(context).succeeded());
    CHECK(module.installedSubSystem() == preexisting);
    CHECK_FALSE(module.ownsRegistration());
    CHECK(factoryCalls == 0);

    module.shutdown(context);
    CHECK(service.findSubSystem("ModuleProbe") == preexisting);
}

TEST_CASE(context_factory_can_resolve_install_time_services)
{
    ProbeModuleService service;
    ayt::module::ModuleContext context;
    CHECK_TRUE(context.bindService(
        std::string(kSubSystemModuleService),
        &service));

    constexpr std::string_view dependencyKey = "test.module.dependency";
    int dependency = 42;
    CHECK_TRUE(context.bindService(
        std::string(dependencyKey),
        &dependency));

    int observedDependency = 0;
    SubSystemModule module(
        probeDescriptor(),
        "ModuleProbe",
        [&observedDependency, dependencyKey](
            ayt::module::IModuleContext& installContext) {
            auto* value = installContext.findServiceAs<int>(dependencyKey);
            if (value == nullptr) {
                return std::unique_ptr<ISubSystem>{};
            }
            observedDependency = *value;
            return std::unique_ptr<ISubSystem>(
                std::make_unique<ProbeSubSystem>());
        });

    CHECK_TRUE(module.install(context).succeeded());
    CHECK(observedDependency == 42);
    CHECK_NOT_NULL(module.installedSubSystem());

    module.shutdown(context);
    CHECK(service.findSubSystem("ModuleProbe") == nullptr);
}

TEST_SUITE_END

} // namespace ayt::game::test
