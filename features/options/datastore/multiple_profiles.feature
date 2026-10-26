@isolated @no_datastore
Feature: Publish and refresh configured shared-memory datasets
    Background:
        Given a deployment configuration with CH car and MLD foot
        And the deployment uses its own shared-memory namespace
        When I prepare the configured profiles

    Scenario: Shared-memory servers start without preparation files
        Then a shared-memory deployment should serve with its release directory hidden

    Scenario: Open-area spatial indexes are available without preparation files
        Given the deployment is rebuilt with an open pedestrian area
        Then a shared-memory deployment should serve with its release directory hidden
        And the shared-memory pedestrian area should be routable

    Scenario: Refresh one profile while preserving the other
        Then a shared-memory release refresh should update the selected profile

    Scenario: Later publication failure preserves earlier successes
        Then a failed second publication should leave the first profile usable

    Scenario: Reject ambiguous or invalid datastore invocations
        Then configured datastore should reject legacy options and file storage
