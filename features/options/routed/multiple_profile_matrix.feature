@isolated @with_mld @with_mmap
Feature: Multi-profile algorithms and loading modes
    Scenario Outline: Both profiles match standalone engines with several workers
        Given a deployment configuration with CH car and MLD foot
        And the deployment uses "<storage>" with "<car>" car and "<foot>" foot
        When I prepare the configured profiles
        Then every deployment service should match its standalone engine

        Examples:
            | storage       | car | foot |
            | mmap          | CH  | CH   |
            | mmap          | MLD | MLD  |
            | mmap          | CH  | MLD  |
            | memory        | CH  | CH   |
            | memory        | MLD | MLD  |
            | memory        | CH  | MLD  |
            | shared_memory | CH  | CH   |
            | shared_memory | MLD | MLD  |
            | shared_memory | CH  | MLD  |
