@isolated @with_mld @with_mmap
Feature: Prepare a multi-profile deployment
    Scenario: Build CH and MLD profiles in a fresh release
        Given a deployment configuration with CH car and MLD foot
        When I prepare the configured profiles
        Then both configured datasets should load independently
        And preparing the same release again should preserve its files and fail

    Scenario: Validate all Lua paths before extraction
        Given a deployment configuration with CH car and MLD foot
        And the second deployment profile has a missing Lua file
        When I prepare the configured profiles
        Then preparation should fail before creating output

    Scenario: Failed preparation cannot be served as a completed release
        Given a deployment configuration with CH car and MLD foot
        And the second deployment profile fails during extraction
        When I prepare the configured profiles
        Then preparation should fail with the profile name and stage
        And the deployment completion descriptor should not exist

    Scenario: Help and version do not need configuration
        Given a deployment configuration with CH car and MLD foot
        Then prepare help and version should work without a configuration

    Scenario: Detect profile directory aliases before extraction
        Given a deployment configuration with CH car and MLD foot
        Then filesystem aliases should be rejected before extraction
