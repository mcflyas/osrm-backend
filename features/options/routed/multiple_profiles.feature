@isolated @with_mld @with_mmap
Feature: Serve multiple configured profiles
    Background:
        Given a deployment configuration with CH car and MLD foot
        When I prepare the configured profiles

    Scenario: All services select their configured engine
        Then every deployment service should match its standalone engine

    Scenario: Only explicit server options override configuration
        Then deployment server options should preserve defaults and honor overrides

    Scenario: All profiles must be complete before startup
        Then incomplete or incompatible releases should fail before readiness

    Scenario: Preparation inputs are unnecessary for serving
        Then a serving-only deployment configuration should initialize

    Scenario: Single-engine flags conflict with the deployment configuration
        Then deployment configuration should reject legacy dataset flags
